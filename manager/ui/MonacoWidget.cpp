#include "MonacoWidget.h"
#include <QWebEngineView>
#include <QWebChannel>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrent>

class MonacoWidget::Bridge : public QObject
{
    Q_OBJECT

public:
    explicit Bridge(MonacoWidget *parent = nullptr) : QObject(parent), m_widget(parent) {}

    Q_INVOKABLE void editorReady()                  { emit editorReadySignal(); }
    Q_INVOKABLE void onTextChanged(const QString &key, const QString &text) { emit textChangedSignal(key, text); }

    Q_INVOKABLE void requestCompletion(const QString &code, int line, int col, int requestId)
    {
        if (!m_widget->m_completionProvider) {
            emit completionResult(requestId, "[]");
            return;
        }
        auto provider = m_widget->m_completionProvider;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, requestId, watcher]() {
            emit completionResult(requestId, watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(provider, code, line, col));
    }

    Q_INVOKABLE void requestSignature(const QString &code, int line, int col, int requestId)
    {
        if (!m_widget->m_signatureProvider) {
            emit signatureResult(requestId, "null");
            return;
        }
        auto provider = m_widget->m_signatureProvider;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, requestId, watcher]() {
            emit signatureResult(requestId, watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(provider, code, line, col));
    }

    Q_INVOKABLE void requestHover(const QString &code, int line, int col, int requestId)
    {
        if (!m_widget->m_hoverProvider) {
            emit hoverResult(requestId, "null");
            return;
        }
        auto provider = m_widget->m_hoverProvider;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, requestId, watcher]() {
            emit hoverResult(requestId, watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(provider, code, line, col));
    }

signals:
    void editorReadySignal();
    void textChangedSignal(const QString &key, const QString &text);
    void eventDataChanged(const QString &json);
    void completionResult(int requestId, const QString &json);
    void signatureResult(int requestId, const QString &json);
    void hoverResult(int requestId, const QString &json);
    void diagnosticsChanged(const QString &json);
    void openDocumentRequested(const QString &key, const QString &text);
    void closeDocumentRequested(const QString &key);
    void renameDocumentRequested(const QString &oldKey, const QString &newKey);
    void showNoDocumentRequested();
    void setReadOnlyRequested(bool readOnly);
    void setThemeRequested(bool dark);

private:
    MonacoWidget *m_widget;
};

MonacoWidget::MonacoWidget(QWidget *parent)
    : QWidget(parent)
{
    m_bridge = new Bridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject("bridge", m_bridge);

    m_view = new QWebEngineView(this);
    m_view->page()->setWebChannel(m_channel);
    m_view->load(QUrl("qrc:///editor.html"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(m_bridge, &Bridge::editorReadySignal, this, &MonacoWidget::onEditorReady);
    connect(m_bridge, &Bridge::textChangedSignal, this, &MonacoWidget::onEditorTextChanged);
}

void MonacoWidget::onEditorReady()
{
    m_pageReady = true;

    if (!m_pendingEventJson.isEmpty()) {
        emit m_bridge->eventDataChanged(m_pendingEventJson);
        m_pendingEventJson.clear();
    }

    emit m_bridge->setThemeRequested(m_pendingDark);
    emit m_bridge->setReadOnlyRequested(m_pendingReadOnly);

    for (const PendingOp &op : std::as_const(m_pendingOps)) {
        switch (op.type) {
        case PendingOp::Open:
            emit m_bridge->openDocumentRequested(op.a, op.b);
            break;
        case PendingOp::Close:
            emit m_bridge->closeDocumentRequested(op.a);
            break;
        case PendingOp::Rename:
            emit m_bridge->renameDocumentRequested(op.a, op.b);
            break;
        case PendingOp::ShowNone:
            emit m_bridge->showNoDocumentRequested();
            break;
        }
    }
    m_pendingOps.clear();
}

void MonacoWidget::onEditorTextChanged(const QString &key, const QString &text)
{
    m_docText.insert(key, text);
    emit textChanged(key);
}

void MonacoWidget::loadEventData(const QString &eventJson)
{
    if (m_pageReady)
        emit m_bridge->eventDataChanged(eventJson);
    else
        m_pendingEventJson = eventJson;
}

void MonacoWidget::setCompletionProvider(std::function<QString(const QString&, int, int)> provider)
{
    m_completionProvider = std::move(provider);
}

void MonacoWidget::setSignatureProvider(std::function<QString(const QString&, int, int)> provider)
{
    m_signatureProvider = std::move(provider);
}

void MonacoWidget::setHoverProvider(std::function<QString(const QString&, int, int)> provider)
{
    m_hoverProvider = std::move(provider);
}

void MonacoWidget::setDiagnostics(const QJsonArray &diagnostics)
{
    QString json = QJsonDocument(diagnostics).toJson(QJsonDocument::Compact);
    if (m_pageReady) {
        emit m_bridge->diagnosticsChanged(json);
    }
}

void MonacoWidget::openDocument(const QString &key, const QString &text)
{
    if (key.isEmpty()) {
        clear();
        return;
    }

    // An already open document keeps its own (possibly unsaved) contents.
    if (!m_docText.contains(key))
        m_docText.insert(key, text);
    m_currentKey = key;

    if (m_pageReady)
        emit m_bridge->openDocumentRequested(key, text);
    else
        m_pendingOps.append({PendingOp::Open, key, text});
}

void MonacoWidget::closeDocument(const QString &key)
{
    m_docText.remove(key);
    if (m_currentKey == key)
        m_currentKey.clear();

    if (m_pageReady)
        emit m_bridge->closeDocumentRequested(key);
    else
        m_pendingOps.append({PendingOp::Close, key, QString()});
}

void MonacoWidget::renameDocument(const QString &oldKey, const QString &newKey)
{
    if (oldKey == newKey || oldKey.isEmpty() || newKey.isEmpty())
        return;

    if (m_docText.contains(oldKey))
        m_docText.insert(newKey, m_docText.take(oldKey));
    if (m_currentKey == oldKey)
        m_currentKey = newKey;

    if (m_pageReady)
        emit m_bridge->renameDocumentRequested(oldKey, newKey);
    else
        m_pendingOps.append({PendingOp::Rename, oldKey, newKey});
}

QString MonacoWidget::getText() const
{
    return m_docText.value(m_currentKey);
}

QString MonacoWidget::documentText(const QString &key) const
{
    return m_docText.value(key);
}

void MonacoWidget::clear()
{
    m_currentKey.clear();

    if (m_pageReady)
        emit m_bridge->showNoDocumentRequested();
    else
        m_pendingOps.append({PendingOp::ShowNone, QString(), QString()});
}

void MonacoWidget::setReadOnly(bool readOnly)
{
    m_pendingReadOnly = readOnly;
    if (m_pageReady)
        emit m_bridge->setReadOnlyRequested(readOnly);
}

void MonacoWidget::setDarkMode(bool dark)
{
    m_pendingDark = dark;
    if (m_pageReady)
        emit m_bridge->setThemeRequested(dark);
}

void MonacoWidget::focus()
{
    if (m_pageReady)
        m_view->page()->runJavaScript("window.editorAPI && window.editorAPI.focus()");
    m_view->setFocus();
}

#include "MonacoWidget.moc"
