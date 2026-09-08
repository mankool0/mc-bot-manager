#ifndef MONACOWIDGET_H
#define MONACOWIDGET_H

#include <QWidget>
#include <QString>
#include <QHash>
#include <QJsonArray>
#include <QList>
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

    // One Monaco model per key, so each document keeps its own text, undo
    // history, cursor and scroll position while the others are hidden.
    // openDocument() only uses text when the document is not open yet;
    // an open document keeps whatever the user has typed into it.
    void openDocument(const QString &key, const QString &text);
    void closeDocument(const QString &key);
    void renameDocument(const QString &oldKey, const QString &newKey);
    QString getText() const;
    QString documentText(const QString &key) const;
    // Show an empty placeholder without closing any document.
    void clear();
    void setReadOnly(bool readOnly);
    void setDarkMode(bool dark);
    void focus();

signals:
    void textChanged(const QString &key);

private slots:
    void onEditorReady();
    void onEditorTextChanged(const QString &key, const QString &text);

private:
    class Bridge;

    QWebEngineView *m_view;
    QWebChannel *m_channel;
    Bridge *m_bridge;
    bool m_pageReady = false;
    // Mirror of the editor contents per document: the page reports every edit
    // with the full text, and openDocument() records what it sent. getText()
    // reads this instead of round-tripping through JavaScript, which needed a
    // nested event loop.
    QHash<QString, QString> m_docText;
    QString m_currentKey;
    // Document commands issued before the page finished loading, replayed in
    // order once it is ready.
    struct PendingOp {
        enum Type { Open, Close, Rename, ShowNone };
        Type type;
        QString a;
        QString b;
    };
    QList<PendingOp> m_pendingOps;
    QString m_pendingEventJson;
    bool m_pendingDark = true;
    bool m_pendingReadOnly = false;

    std::function<QString(const QString&, int, int)> m_completionProvider;
    std::function<QString(const QString&, int, int)> m_signatureProvider;
    std::function<QString(const QString&, int, int)> m_hoverProvider;
};

#endif // MONACOWIDGET_H
