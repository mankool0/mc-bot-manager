#ifndef MONACOWIDGET_H
#define MONACOWIDGET_H

#include <QWidget>
#include <QString>
#include <QJsonArray>
#include <functional>

class QWebEngineView;
class QWebChannel;

class MonacoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MonacoWidget(QWidget *parent = nullptr);

    void loadEventData(const QString &eventJson);
    void setCompletionProvider(std::function<QString(const QString&, int, int)> provider);
    void setSignatureProvider(std::function<QString(const QString&, int, int)> provider);
    void setHoverProvider(std::function<QString(const QString&, int, int)> provider);
    void setDiagnostics(const QJsonArray &diagnostics);
    void setText(const QString &text);
    QString getText() const;
    void clear();
    void setReadOnly(bool readOnly);
    void setDarkMode(bool dark);
    void focus();

signals:
    void textChanged();

private slots:
    void onEditorReady();
    void onEditorTextChanged(const QString &text);

private:
    class Bridge;

    QWebEngineView *m_view;
    QWebChannel *m_channel;
    Bridge *m_bridge;
    bool m_pageReady = false;
    // Mirror of the editor contents: the page reports every edit with the full
    // text, and setText() records what it sent. getText() reads this instead of
    // round-tripping through JavaScript, which needed a nested event loop.
    QString m_text;
    bool m_textPendingPush = false;
    QString m_pendingEventJson;
    bool m_pendingDark = true;
    bool m_pendingReadOnly = false;

    std::function<QString(const QString&, int, int)> m_completionProvider;
    std::function<QString(const QString&, int, int)> m_signatureProvider;
    std::function<QString(const QString&, int, int)> m_hoverProvider;
};

#endif // MONACOWIDGET_H
