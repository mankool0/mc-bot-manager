#include "ZubanClient.h"
#include "logging/LogManager.h"
#include "EmbeddedPythonLibs.h"
#include <QProcess>
#include <QJsonDocument>
#include <QMetaObject>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QUrl>
#include <chrono>

ZubanClient::ZubanClient(QObject *parent)
    : QObject(parent)
{
}

ZubanClient::~ZubanClient()
{
    stop();
}

void ZubanClient::start(const QString &scriptsDir, const QString &stubsDir, const QString &pylibsDir)
{
    stop();
    // Place the virtual document inside the workspace so Zuban analyses it in context
    m_docUri = QUrl::fromLocalFile(scriptsDir + "/current.py").toString();

    QStringList extraPaths;

    // AppImage: Python and pip-installed tools live under $APPDIR/usr/bin
    QByteArray appdirEnv = qgetenv("APPDIR");
    if (!appdirEnv.isEmpty()) {
        extraPaths << QString::fromLocal8Bit(appdirEnv) + "/usr/bin";
    }

    // Bundled Python next to the app binary (Windows release layout)
    QString appBinDir = QCoreApplication::applicationDirPath();
    extraPaths << appBinDir + "/Scripts"
               << appBinDir;

    QString zubanPath = QStandardPaths::findExecutable("zuban", extraPaths);
#ifndef NDEBUG
    if (zubanPath.isEmpty()) {
        zubanPath = QStandardPaths::findExecutable("zuban");
    }
#endif
    if (zubanPath.isEmpty()) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            LogManager::log("Zuban not found - Python LSP features disabled", LogManager::Warning);
        }
        return;
    }

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyRead, this, &ZubanClient::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &ZubanClient::onProcessError);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList stubPaths = {stubsDir, pylibsDir, scriptsDir};
    QChar sep = QDir::listSeparator();
    QString pythonPath = stubPaths.join(sep);
    QString existing = env.value("PYTHONPATH");
    if (!existing.isEmpty()) {
        pythonPath += sep + existing;
    }
    env.insert("PYTHONPATH", pythonPath);
    m_process->setProcessEnvironment(env);

    m_process->start(zubanPath, {"server"});
    if (!m_process->waitForStarted(3000)) {
        LogManager::log("Failed to start zuban server - Python LSP features disabled", LogManager::Warning);
        delete m_process;
        m_process = nullptr;
        return;
    }

    sendInitialize(scriptsDir);
}

void ZubanClient::stop()
{
    m_initialized = false;
    m_lastCode.clear();
    m_docVersion = 0;
    m_nextId = 1;

    for (auto &p : m_pending) {
        p->set_value(QJsonObject{});
    }
    m_pending.clear();

    if (m_process) {
        m_process->terminate();
        m_process->waitForFinished(1000);
        delete m_process;
        m_process = nullptr;
    }
}

void ZubanClient::sendRaw(const QJsonObject &msg)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        return;
    }
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray header = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    m_process->write(header + body);
}

void ZubanClient::sendInitialize(const QString &scriptsDir)
{
    QJsonObject capabilities;
    QJsonObject textDocument;
    textDocument["completion"] = QJsonObject{};
    textDocument["signatureHelp"] = QJsonObject{};
    textDocument["hover"] = QJsonObject{};
    textDocument["publishDiagnostics"] = QJsonObject{};
    capabilities["textDocument"] = textDocument;

    QJsonObject params;
    params["processId"] = static_cast<int>(QCoreApplication::applicationPid());
    params["rootUri"] = QUrl::fromLocalFile(scriptsDir).toString();
    params["capabilities"] = capabilities;

    sendRaw({{"jsonrpc", "2.0"}, {"id", m_nextId++}, {"method", "initialize"}, {"params", params}});
}

void ZubanClient::syncDocument(const QString &code)
{
    if (code == m_lastCode) {
        return;
    }

    bool isFirst = m_lastCode.isEmpty();
    m_lastCode = code;
    m_docVersion++;

    QJsonObject textDocument;
    textDocument["uri"] = m_docUri;

    if (isFirst) {
        textDocument["languageId"] = "python";
        textDocument["version"] = m_docVersion;
        textDocument["text"] = code;
        QJsonObject params;
        params["textDocument"] = textDocument;
        sendRaw({{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"}, {"params", params}});
    } else {
        textDocument["version"] = m_docVersion;
        QJsonObject change;
        change["text"] = code;
        QJsonObject params;
        params["textDocument"] = textDocument;
        params["contentChanges"] = QJsonArray{change};
        sendRaw({{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"}, {"params", params}});
    }
}

void ZubanClient::onReadyRead()
{
    m_readBuf += m_process->readAll();
    processBuffer();
}

void ZubanClient::processBuffer()
{
    while (true) {
        int sep = m_readBuf.indexOf("\r\n\r\n");
        if (sep < 0) {
            break;
        }

        QByteArray header = m_readBuf.left(sep);
        int contentLength = -1;
        for (const QByteArray &line : header.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                contentLength = trimmed.mid(15).trimmed().toInt();
                break;
            }
        }
        if (contentLength < 0) {
            break;
        }

        int bodyStart = sep + 4;
        if (m_readBuf.size() < bodyStart + contentLength) {
            break;
        }

        QByteArray body = m_readBuf.mid(bodyStart, contentLength);
        m_readBuf.remove(0, bodyStart + contentLength);

        QJsonObject msg = QJsonDocument::fromJson(body).object();
        if (msg.isEmpty())
            continue;

        QString method = msg["method"].toString();
        bool hasId = msg.contains("id");

        if (hasId && !method.isEmpty()) {
            // Server-initiated request - not handled
        } else if (hasId) {
            int id = msg["id"].toInt();
            if (!m_initialized && method.isEmpty()) {
                // First response is always the initialize response
                m_initialized = true;
                sendRaw({{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", QJsonObject{}}});
            }
            handleResponse(id, msg["result"], msg["error"]);
        } else if (!method.isEmpty()) {
            handleNotification(method, msg["params"].toObject());
        }
    }
}

void ZubanClient::handleResponse(int id, const QJsonValue &result, const QJsonValue &error)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    PendingPromise promise = it.value();
    m_pending.erase(it);
    bool hasError = error.isObject() && !error.toObject().isEmpty();
    promise->set_value(hasError ? QJsonValue{} : result);
}

void ZubanClient::handleNotification(const QString &method, const QJsonObject &params)
{
    if (method == "textDocument/publishDiagnostics") {
        emit diagnosticsReceived(params["diagnostics"].toArray());
    }
}

void ZubanClient::onProcessError(QProcess::ProcessError)
{
    LogManager::log("Zuban server process error - Python LSP features disabled", LogManager::Warning);
    m_initialized = false;
}

QJsonValue ZubanClient::sendRequest(const QString &code, const QString &method,
                                    const QJsonObject &params, int timeoutMs)
{
    if (!m_initialized) {
        return {};
    }

    auto promise = std::make_shared<std::promise<QJsonValue>>();
    auto future = promise->get_future();

    QMetaObject::invokeMethod(this, [this, code, method, params, promise]() {
        if (!m_initialized) {
            promise->set_value(QJsonValue{});
            return;
        }
        syncDocument(code);
        int id = m_nextId++;
        m_pending[id] = promise;
        sendRaw({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
    }, Qt::QueuedConnection);

    auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
    if (status != std::future_status::ready) {
        LogManager::log(QString("Zuban: request '%1' timed out after %2ms").arg(method).arg(timeoutMs),
                        LogManager::Warning);
        QMetaObject::invokeMethod(this, [this, promise]() {
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it.value() == promise) {
                    m_pending.erase(it);
                    break;
                }
            }
        }, Qt::QueuedConnection);
        return {};
    }

    return future.get();
}

QString ZubanClient::complete(const QString &code, int line, int col)
{
    if (!m_initialized) {
        return "[]";
    }

    QJsonObject textDocument;
    textDocument["uri"] = m_docUri;
    QJsonObject position;
    position["line"] = line - 1;  // Monaco lineNumber is 1-based, LSP is 0-based
    position["character"] = col;  // JS already passes column - 1 (0-based)
    QJsonObject params;
    params["textDocument"] = textDocument;
    params["position"] = position;

    QJsonValue result = sendRequest(code, "textDocument/completion", params);
    if (result.isNull() || result.isUndefined()) {
        return "[]";
    }

    QJsonArray items;
    if (result.isArray()) {
        items = result.toArray();
    } else if (result.isObject()) {
        QJsonObject obj = result.toObject();
        if (obj.contains("items"))
            items = obj["items"].toArray();
    }

    auto toMonacoKind = [](int lspKind) -> int {
        switch (lspKind) {
        case 3:  return 1;  // Function
        case 7:  return 5;  // Class
        case 9:  return 8;  // Module
        case 14: return 17; // Keyword
        case 5:  return 9;  // Field/Property
        case 6:  return 9;  // Variable
        default: return 4;
        }
    };

    QJsonArray out;
    int index = 0;
    for (const QJsonValue &v : items) {
        QJsonObject item = v.toObject();
        QString label = item["label"].toString();
        QString insertText = item.contains("insertText") ? item["insertText"].toString() : label;
        QString detail = item["detail"].toString();

        QJsonObject ci;
        ci["label"] = label;
        ci["insertText"] = insertText;
        ci["kind"] = toMonacoKind(item["kind"].toInt(4));
        ci["sortText"] = QString("%1").arg(index++, 5, 10, QChar('0'));
        if (!detail.isEmpty()) {
            ci["detail"] = detail.length() > 80 ? detail.left(77) + "..." : detail;
        }
        out.append(ci);
    }
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString ZubanClient::signatureHelp(const QString &code, int line, int col)
{
    if (!m_initialized) {
        return "null";
    }

    QJsonObject textDocument;
    textDocument["uri"] = m_docUri;
    QJsonObject position;
    position["line"] = line - 1;
    position["character"] = col;
    QJsonObject params;
    params["textDocument"] = textDocument;
    params["position"] = position;

    QJsonValue result = sendRequest(code, "textDocument/signatureHelp", params);
    if (!result.isObject()) {
        return "null";
    }

    QJsonObject resultObj = result.toObject();
    QJsonArray sigs = resultObj["signatures"].toArray();
    if (sigs.isEmpty()) {
        return "null";
    }

    QJsonObject sig = sigs[0].toObject();
    QString sigLabel = sig["label"].toString();
    int activeParam = resultObj["activeParameter"].toInt(0);

    // Zuban omits the function name and replaces all defaults with '...' in sigLabel.
    // Extract the function name from the code and look up the full signature from stubs.
    QString funcName;
    const QStringList codeLines = code.split('\n');
    if (line - 1 >= 0 && line - 1 < codeLines.size()) {
        const QString &lineStr = codeLines[line - 1];
        int depth = 0;
        for (int i = col - 1; i >= 0; i--) {
            QChar c = lineStr[i];
            if (c == ')') { depth++; }
            else if (c == '(') {
                if (depth == 0) {
                    int end = i;
                    int start = end - 1;
                    while (start >= 0 && (lineStr[start].isLetterOrNumber() || lineStr[start] == '_'))
                        start--;
                    funcName = lineStr.mid(start + 1, end - start - 1);
                    break;
                }
                depth--;
            }
        }
    }

    if (!funcName.isEmpty()) {
        const QStringList modules = EmbeddedPythonLibs::getStubModuleNames();
        QRegularExpression re(
            QString(R"(def %1\(([^)]*)\)\s*->\s*([^:\n]+):)")
                .arg(QRegularExpression::escape(funcName)));
        for (const QString &mod : modules) {
            QFile f(QString(":/stubs/%1.pyi").arg(mod));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            auto m = re.match(QString::fromUtf8(f.readAll()));
            if (m.hasMatch()) {
                sigLabel = QString("%1(%2) -> %3").arg(funcName, m.captured(1), m.captured(2).trimmed());
                break;
            }
        }
    }

    // Split params from sigLabel at depth-0 commas so defaults are included.
    QJsonArray paramsArr;
    QStringList paramLabels;
    int open = sigLabel.indexOf('(');
    int close = sigLabel.lastIndexOf(')');
    if (open >= 0 && close > open) {
        QString paramsStr = sigLabel.mid(open + 1, close - open - 1);
        int depth = 0, start = 0;
        for (int i = 0; i <= paramsStr.length(); i++) {
            if (i == paramsStr.length() || (paramsStr[i] == ',' && depth == 0)) {
                QString p = paramsStr.mid(start, i - start).trimmed();
                if (!p.isEmpty()) { paramsArr.append(p); paramLabels << p; }
                start = i + 1;
            } else if (paramsStr[i] == '(' || paramsStr[i] == '[') {
                depth++;
            } else if (paramsStr[i] == ')' || paramsStr[i] == ']') {
                depth--;
            }
        }
    }

    QJsonObject out;
    out["label"] = sigLabel;
    out["params"] = paramsArr;
    out["activeParam"] = qMax(0, qMin(activeParam, (int)paramsArr.size() - 1));
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString ZubanClient::hover(const QString &code, int line, int col)
{
    if (!m_initialized) {
        return "null";
    }

    QJsonObject textDocument;
    textDocument["uri"] = m_docUri;
    QJsonObject position;
    position["line"] = line - 1;
    position["character"] = col;
    QJsonObject params;
    params["textDocument"] = textDocument;
    params["position"] = position;

    QJsonValue result = sendRequest(code, "textDocument/hover", params);
    if (!result.isObject()) {
        return "null";
    }

    QJsonObject resultObj = result.toObject();
    if (!resultObj.contains("contents")) {
        return "null";
    }

    QString docstring;
    QJsonValue contents = resultObj["contents"];
    if (contents.isObject()) {
        docstring = contents.toObject()["value"].toString();
    } else if (contents.isString()) {
        docstring = contents.toString();
    } else if (contents.isArray()) {
        QStringList parts;
        for (const QJsonValue &cv : contents.toArray()) {
            if (cv.isString()) {
                parts << cv.toString();
            } else if (cv.isObject()) {
                parts << cv.toObject()["value"].toString();
            }
        }
        docstring = parts.join("\n\n");
    }

    if (docstring.isEmpty()) {
        return "null";
    }

    QJsonObject out;
    out["docstring"] = docstring;
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}
