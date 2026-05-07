#ifndef ZUBANCLIENT_H
#define ZUBANCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QMutex>
#include <QProcess>
#include <memory>
#include <future>

class ZubanClient : public QObject
{
    Q_OBJECT

public:
    explicit ZubanClient(QObject *parent = nullptr);
    ~ZubanClient();

    void start(const QString &scriptsDir, const QString &stubsDir, const QString &pylibsDir);
    void stop();
    bool isReady() const { return m_initialized; }

    // Thread-safe: called from QtConcurrent worker threads, blocks until response or timeout
    QString complete(const QString &code, int line, int col);
    QString signatureHelp(const QString &code, int line, int col);
    QString hover(const QString &code, int line, int col);

signals:
    void diagnosticsReceived(const QJsonArray &diagnostics);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);

private:
    // Run on main thread only
    void sendRaw(const QJsonObject &msg);
    void processBuffer();
    void handleResponse(int id, const QJsonValue &result, const QJsonValue &error);
    void handleNotification(const QString &method, const QJsonObject &params);
    void syncDocument(const QString &code);
    void sendInitialize(const QString &scriptsDir);

    QJsonValue sendRequest(const QString &code, const QString &method,
                           const QJsonObject &params, int timeoutMs = 2000);

    QProcess *m_process = nullptr;
    QByteArray m_readBuf;
    int m_nextId = 1;
    bool m_initialized = false;

    QString m_docUri;
    QString m_lastCode;
    int m_docVersion = 0;

    using PendingPromise = std::shared_ptr<std::promise<QJsonValue>>;
    QMap<int, PendingPromise> m_pending;
};

#endif // ZUBANCLIENT_H
