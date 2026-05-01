#include "MonacoWidget.h"
#include <QWebEngineView>
#include <QWebChannel>
#include <QVBoxLayout>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

class MonacoWidget::Bridge : public QObject
{
    Q_OBJECT

public:
    explicit Bridge(MonacoWidget *parent = nullptr) : QObject(parent), m_widget(parent) {}

    Q_INVOKABLE void editorReady()                  { emit editorReadySignal(); }
    Q_INVOKABLE void onTextChanged(const QString &) { emit textChangedSignal(); }

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
    void textChangedSignal();
    void eventDataChanged(const QString &json);
    void completionResult(int requestId, const QString &json);
    void signatureResult(int requestId, const QString &json);
    void hoverResult(int requestId, const QString &json);
    void setTextRequested(const QString &text);
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
    connect(m_bridge, &Bridge::textChangedSignal, this, &MonacoWidget::textChanged);
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

    if (!m_pendingText.isEmpty()) {
        emit m_bridge->setTextRequested(m_pendingText);
        m_pendingText.clear();
    }
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

void MonacoWidget::setText(const QString &text)
{
    if (m_pageReady)
        emit m_bridge->setTextRequested(text);
    else
        m_pendingText = text;
}

QString MonacoWidget::getText() const
{
    if (!m_pageReady)
        return m_pendingText;

    QString result;
    QEventLoop loop;
    m_view->page()->runJavaScript(
        "window.editorAPI ? window.editorAPI.getText() : ''",
        [&result, &loop](const QVariant &v) {
            result = v.toString();
            loop.quit();
        });
    loop.exec();
    return result;
}

void MonacoWidget::clear()
{
    setText(QString());
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
