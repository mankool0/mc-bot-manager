#include "ScriptEngine.h"
#include "ScriptContext.h"
#include "ScriptThread.h"
#include "ScriptFileManager.h"
#include "PythonAPI.h"
#include "EmbeddedPythonLibs.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "ui/AppColors.h"
#include "logging/LogManager.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include <QWriteLocker>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdlib>

namespace py = pybind11;

bool ScriptEngine::pythonInitialized = false;
int ScriptEngine::engineCount = 0;
PyThreadState *ScriptEngine::mainThreadState = nullptr;

ScriptEngine::ScriptEngine(BotInstance *bot, QObject *parent)
    : QObject(parent), botInstance(bot)
{
    if (engineCount == 0) {
        initializePython();
    }
    engineCount++;

    qRegisterMetaType<ScriptEvent>("ScriptEvent");
    qRegisterMetaType<ScriptContext*>("ScriptContext*");

    m_eventWorkerThread = new QThread(this);
    m_eventWorker = new ScriptEventWorker(bot);
    m_eventWorker->moveToThread(m_eventWorkerThread);
    connect(m_eventWorkerThread, &QThread::finished, m_eventWorker, &QObject::deleteLater);
    connect(this, &ScriptEngine::eventReady,
            m_eventWorker, &ScriptEventWorker::processEvent,
            Qt::QueuedConnection);
    m_eventWorkerThread->start();
}

ScriptEngine::~ScriptEngine()
{
    QMap<QString, ScriptState> states;
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptState state;
        state.autorun = it.value()->enabled;
        states[it.key()] = state;
    }
    ScriptFileManager::saveScriptStates(botInstance->name, states);

    stopAllScripts();

    // Drain the event worker queue before destroying Python objects.
    m_eventWorkerThread->quit();
    m_eventWorkerThread->wait();

    {
        py::gil_scoped_acquire acquire;
        qDeleteAll(scripts);
        scripts.clear();
    }

    engineCount--;
    if (engineCount == 0) {
        cleanupPython();
    }
}

void ScriptEngine::loadScriptsFromDisk()
{
    QStringList scriptFiles = ScriptFileManager::listScripts(botInstance->name);
    QMap<QString, ScriptState> states = ScriptFileManager::loadScriptStates(botInstance->name);

    for (const QString &filename : std::as_const(scriptFiles)) {
        QString code = ScriptFileManager::loadScript(botInstance->name, filename);
        if (!code.isEmpty()) {
            loadScript(filename, code);

            if (states.contains(filename)) {
                enableScript(filename, states[filename].autorun);

                // Auto-run scripts that are set to autorun
                if (states[filename].autorun) {
                    runScript(filename);
                }
            }
        }
    }
}

void ScriptEngine::initializePython()
{
    if (pythonInitialized) return;

    try {
#ifndef _WIN32
        if (const char *appdir = getenv("APPDIR")) {
            static std::string pythonHome = std::string(appdir) + "/usr";
            setenv("PYTHONHOME", pythonHome.c_str(), 1);
        }
#endif
        py::initialize_interpreter();
        setupPythonPath();

        // Release the GIL so script threads can acquire it
        // The main thread doesn't need the GIL unless it's executing Python code
        mainThreadState = PyEval_SaveThread();

        pythonInitialized = true;
    } catch (py::error_already_set &e) {
        LogManager::log(QString("Failed to initialize Python: %1").arg(e.what()), LogManager::Error);
    }
}

void ScriptEngine::cleanupPython()
{
    if (!pythonInitialized) return;

    try {
        // Re-acquire the GIL before finalizing
        if (mainThreadState) {
            PyEval_RestoreThread(mainThreadState);
            mainThreadState = nullptr;
        }

        py::finalize_interpreter();
        pythonInitialized = false;
    } catch (py::error_already_set &e) {
        LogManager::log(QString("Error during Python cleanup: %1").arg(e.what()), LogManager::Error);
    }
}

void ScriptEngine::setupPythonPath()
{
    py::module_ sys = py::module_::import("sys");
    sys.attr("dont_write_bytecode") = true;
    py::list path = sys.attr("path");

    QString scriptsBaseDir = ScriptFileManager::getBaseScriptDir();
    path.append(scriptsBaseDir.toStdString());

    QString internalLibsDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/pylibs";
    QDir().mkpath(internalLibsDir);
    path.append(internalLibsDir.toStdString());

    EmbeddedPythonLibs::copyPublicModules(scriptsBaseDir);
    EmbeddedPythonLibs::copyInternalModules(internalLibsDir);
    EmbeddedPythonLibs::ensureJediAvailable(internalLibsDir);
}

bool ScriptEngine::loadScript(const QString &filename, const QString &code)
{
    bool wasEnabled = false;
    if (scripts.contains(filename)) {
        wasEnabled = scripts[filename]->enabled;
        unloadScript(filename);
    }

    py::gil_scoped_acquire acquire;

    ScriptContext *ctx = new ScriptContext();
    ctx->filename = filename;
    ctx->code = code;
    ctx->enabled = wasEnabled;
    ctx->running = false;

    scripts[filename] = ctx;
    emit scriptLoaded(filename);

    return true;
}

void ScriptEngine::unloadScript(const QString &filename)
{
    if (!scripts.contains(filename)) return;

    stopScript(filename);

    ScriptContext *ctx = scripts.take(filename);

    py::gil_scoped_acquire acquire;
    delete ctx;

    emit scriptUnloaded(filename);
}

bool ScriptEngine::reloadScript(const QString &filename)
{
    if (!scripts.contains(filename)) return false;

    ScriptContext *ctx = scripts[filename];
    return loadScript(filename, ctx->code);
}

void ScriptEngine::enableScript(const QString &filename, bool enabled)
{
    if (!scripts.contains(filename)) return;

    scripts[filename]->enabled = enabled;
}

bool ScriptEngine::runScript(const QString &filename)
{
    if (!scripts.contains(filename)) return false;

    ScriptContext *ctx = scripts[filename];
    if (ctx->running || ctx->thread) return false;

    if (botInstance->consoleWidget) {
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
        botInstance->consoleWidget->appendOutput(
            QString("[%1] [%2] Starting...").arg(ts, filename), Qt::darkCyan);
    }

    ctx->running = true;
    ctx->thread = new ScriptThread(ctx, botInstance, this);

    // Connect thread cleanup - delete when thread finishes naturally
    connect(ctx->thread, &QThread::finished, ctx->thread, &QObject::deleteLater, Qt::UniqueConnection);

    // Connect thread signals (use Qt::QueuedConnection to ensure GUI updates happen on main thread)
    connect(ctx->thread, &ScriptThread::scriptFinished, this, [this, filename](bool success) {
        if (!scripts.contains(filename)) return;

        ScriptContext *ctx = scripts[filename];
        ctx->thread = nullptr; // Don't delete here, finished signal handles it

        if (success) {
            if (ctx->eventHandlers.isEmpty()) {
                ctx->running = false;
                if (botInstance->consoleWidget) {
                    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
                    botInstance->consoleWidget->appendOutput(
                        QString("[%1] [%2] Completed successfully").arg(ts, filename), AppColors::scriptSuccess());
                }
                emit scriptStopped(filename);
            }
        } else {
            // Script stopped (error or user interruption)
            ctx->running = false;

            // Show stopped message if no error was set (user stopped)
            if (ctx->lastError.isEmpty() && botInstance->consoleWidget) {
                QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
                botInstance->consoleWidget->appendOutput(
                    QString("[%1] [%2] Stopped").arg(ts, filename), AppColors::scriptStopped());
            }

            emit scriptStopped(filename);
        }
    }, Qt::QueuedConnection);

    connect(ctx->thread, &ScriptThread::scriptError, this, [this, filename](const QString &error) {
        if (!scripts.contains(filename)) return;

        emit scriptError(filename, error);

        if (botInstance->consoleWidget) {
            QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
            botInstance->consoleWidget->appendOutput(
                QString("[%1] [Script Error in %2]").arg(ts, filename),
                AppColors::scriptError()
            );
            QStringList errorLines = error.split("\n");
            for (const QString &line : std::as_const(errorLines)) {
                botInstance->consoleWidget->appendOutput(line, AppColors::scriptError());
            }
        }
    }, Qt::QueuedConnection);

    connect(ctx->thread, &ScriptThread::scriptMessage, this, [this, filename](const QString &message) {
        if (!scripts.contains(filename)) return;

        if (botInstance->consoleWidget) {
            botInstance->consoleWidget->appendOutput(message, AppColors::scriptLog());
        }
    }, Qt::QueuedConnection);

    ctx->thread->start();
    emit scriptStarted(filename);

    return true;
}

void ScriptEngine::stopScript(const QString &filename)
{
    if (!scripts.contains(filename)) return;

    ScriptContext *ctx = scripts[filename];
    if (!ctx->running) return;

    if (ctx->thread) {
        // Imperative script still running in thread - signal it to stop
        ctx->thread->stop();

        if (botInstance->consoleWidget) {
            QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString msg = QString("[%1] [%2] Sending stop signal...").arg(ts, filename);
            QMetaObject::invokeMethod(botInstance->consoleWidget, [widget = botInstance->consoleWidget, msg]() {
                widget->appendOutput(msg, Qt::darkYellow);
            }, Qt::QueuedConnection);
        }
    } else {
        // Event-driven script (thread finished but event handlers registered)
        ctx->running = false;

        {
            // Lock order: handlersLock first, then GIL. Consistent with processEvent().
            QWriteLocker locker(&ctx->handlersLock);
            py::gil_scoped_acquire acquire;
            ctx->eventHandlers.clear();
        }

        if (botInstance->consoleWidget) {
            QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
            botInstance->consoleWidget->appendOutput(
                QString("[%1] [%2] Stopped").arg(ts, filename), Qt::darkYellow);
        }

        emit scriptStopped(filename);
    }
}

void ScriptEngine::stopAllScripts()
{
    // Signal all scripts to stop
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        stopScript(it.key());
    }

    // Wait for all threads to finish
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptContext *ctx = it.value();
        if (ctx->thread && ctx->thread->isRunning()) {
            ctx->thread->wait();
        }
    }
}

void ScriptEngine::fireEvent(const QString &eventName, const QVariantList &args)
{
    // Called on the main thread. Must not block - post to worker thread instead.
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptContext *ctx = it.value();

        if (!ctx->running)
            continue;

        if (!ctx->eventHandlers.contains(eventName))
            continue;

        ScriptEvent event;
        event.scriptFilename = ctx->filename;
        event.eventName = eventName;
        event.args = args;
        event.botName = botInstance->name;

        emit eventReady(event, ctx);
    }
}

void ScriptEngine::fireEvent(const QString &eventName, std::function<void(void*)> argBuilder)
{
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptContext *ctx = it.value();

        if (!ctx->running)
            continue;

        if (!ctx->eventHandlers.contains(eventName))
            continue;

        ScriptEvent event;
        event.scriptFilename = ctx->filename;
        event.eventName = eventName;
        event.botName = botInstance->name;
        event.argBuilder = argBuilder;

        emit eventReady(event, ctx);
    }
}

QStringList ScriptEngine::getScriptNames() const
{
    return scripts.keys();
}

ScriptContext* ScriptEngine::getScript(const QString &filename)
{
    return scripts.value(filename, nullptr);
}

bool ScriptEngine::isScriptEnabled(const QString &filename) const
{
    if (!scripts.contains(filename)) return false;
    return scripts[filename]->enabled;
}

bool ScriptEngine::isScriptRunning(const QString &filename) const
{
    if (!scripts.contains(filename)) return false;
    return scripts[filename]->running;
}

QString ScriptEngine::getScriptError(const QString &filename) const
{
    if (!scripts.contains(filename)) return QString();
    return scripts[filename]->lastError;
}

QString ScriptEngine::getBotName() const
{
    return botInstance ? botInstance->name : QString();
}

static const char* JEDI_TYPE_PREAMBLE =
"from _events import *\n"
"def on(event_name: str): ...\n"
"\n";

static py::object makeJediScript(const QString &code)
{
    using namespace py::literals;
    py::module_ jedi = py::module_::import("jedi");
    py::object environment = jedi.attr("InterpreterEnvironment")();
    std::string projectPath = ScriptFileManager::getBaseScriptDir().toStdString();
    py::object project = jedi.attr("Project")(projectPath);
    QString augmented = QString::fromLatin1(JEDI_TYPE_PREAMBLE) + code;
    return jedi.attr("Script")(augmented.toStdString(),
        "project"_a = project, "environment"_a = environment);
}

QString ScriptEngine::loadEventData()
{
    QJsonArray events;
    QJsonObject eventParams;

    if (pythonInitialized) {
        try {
            py::gil_scoped_acquire acquire;
            py::module_ evMod = py::module_::import("_events");
            py::dict paramsDict = evMod.attr("EVENT_HANDLER_PARAMS");
            for (auto pair : paramsDict) {
                QString evName = QString::fromStdString(pair.first.cast<std::string>());
                QString evParamStr = QString::fromStdString(pair.second.cast<std::string>());
                QJsonObject item;
                item["label"] = evName;
                item["insertText"] = evName;
                item["detail"] = "event";
                item["kind"] = 13;
                events.append(item);
                eventParams[evName] = evParamStr;
            }
        } catch (py::error_already_set &e) {
            qWarning() << "loadEventData: failed to load _events:" << e.what();
        }
    }

    QJsonObject root;
    root["events"] = events;
    root["event_params"] = eventParams;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString ScriptEngine::jediComplete(const QString &code, int line, int col)
{
    if (!pythonInitialized) return "[]";
    py::gil_scoped_acquire acquire;
    try {
        static const int PREAMBLE_LINES = QByteArray(JEDI_TYPE_PREAMBLE).count('\n');
        py::object script = makeJediScript(code);
        py::list completions = script.attr("complete")(line + PREAMBLE_LINES, col);

        auto toMonacoKind = [](const std::string &type) -> int {
            if (type == "function") return 1;
            if (type == "class")    return 5;
            if (type == "module")   return 8;
            if (type == "keyword")  return 17;
            if (type == "param")    return 9;
            if (type == "property") return 9;
            if (type == "path")     return 20;
            return 4;
        };

        QJsonArray result;
        int index = 0;
        for (auto compHandle : completions) {
            py::object comp = compHandle.cast<py::object>();
            QString name = QString::fromStdString(comp.attr("name").cast<std::string>());
            std::string type = comp.attr("type").cast<std::string>();
            QString description = QString::fromStdString(comp.attr("description").cast<std::string>());
            QJsonObject item;
            item["label"] = name;
            item["insertText"] = name;
            item["kind"] = toMonacoKind(type);
            item["sortText"] = QString("%1").arg(index++, 5, 10, QChar('0'));
            if (!description.isEmpty())
                item["detail"] = description.length() > 80 ? description.left(77) + "..." : description;
            result.append(item);
        }
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    } catch (py::error_already_set &e) {
        qWarning() << "Jedi completion error:" << e.what();
        return "[]";
    } catch (...) {
        return "[]";
    }
}

QString ScriptEngine::jediSignature(const QString &code, int line, int col)
{
    if (!pythonInitialized) return "null";
    py::gil_scoped_acquire acquire;
    try {
        static const int PREAMBLE_LINES = QByteArray(JEDI_TYPE_PREAMBLE).count('\n');
        py::object script = makeJediScript(code);
        py::list sigs = script.attr("get_signatures")(line + PREAMBLE_LINES, col);

        if (sigs.empty()) return "null";
        py::object sig = sigs[0];

        QString name = QString::fromStdString(sig.attr("name").cast<std::string>());
        int activeParam = sig.attr("index").is_none() ? 0 : sig.attr("index").cast<int>();

        py::list params = sig.attr("params");
        QJsonArray paramsArr;
        QStringList paramLabels;
        for (auto p : params) {
            py::object param = p.cast<py::object>();
            QString desc = QString::fromStdString(param.attr("description").cast<std::string>());
            if (desc.startsWith("param ")) desc = desc.mid(6);
            paramsArr.append(desc);
            paramLabels << desc;
        }

        QJsonObject result;
        result["label"] = name + "(" + paramLabels.join(", ") + ")";
        result["params"] = paramsArr;
        result["activeParam"] = qMax(0, qMin(activeParam, (int)paramsArr.size() - 1));
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    } catch (py::error_already_set &e) {
        qWarning() << "Jedi signature error:" << e.what();
        return "null";
    } catch (...) {
        return "null";
    }
}

QString ScriptEngine::jediHelp(const QString &code, int line, int col)
{
    if (!pythonInitialized) return "null";
    py::gil_scoped_acquire acquire;
    try {
        static const int PREAMBLE_LINES = QByteArray(JEDI_TYPE_PREAMBLE).count('\n');
        py::object script = makeJediScript(code);
        py::list names = script.attr("help")(line + PREAMBLE_LINES, col);

        if (names.empty()) return "null";
        py::object name = names[0];

        QString fullName = QString::fromStdString(name.attr("full_name").is_none()
            ? name.attr("name").cast<std::string>()
            : name.attr("full_name").cast<std::string>());
        QString type = QString::fromStdString(name.attr("type").cast<std::string>());
        QString docstring = QString::fromStdString(name.attr("docstring")().cast<std::string>());

        QJsonObject result;
        result["fullName"] = fullName;
        result["type"] = type;
        result["docstring"] = docstring;
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    } catch (py::error_already_set &e) {
        qWarning() << "Jedi help error:" << e.what();
        return "null";
    } catch (...) {
        return "null";
    }
}

