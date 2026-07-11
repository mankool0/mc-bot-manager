#ifndef SCRIPTMESSAGEBUS_H
#define SCRIPTMESSAGEBUS_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QSemaphore>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class ScriptEngine;
struct ScriptContext;

// Result slot for a script-thread request that runs on the main thread
// (manager.run_script and friends). Heap-shared so a timed-out caller can
// return while the queued lambda still fills it in later.
struct PendingScriptOp {
    QSemaphore sem;
    bool ok = false;
    QString error;
    QVariant result;
};

// Routes messages between scripts across all engines (per-bot and _global).
// Engines and endpoints (running scripts) are registered from the main thread;
// publish/sendTo/subscribe/openMailbox are called from script threads.
//
// Lock order: m_mutex, then ScriptContext::handlersLock, then the GIL.
// Callers must NOT hold the GIL when calling into the bus.
class ScriptMessageBus : public QObject
{
    Q_OBJECT

public:
    static ScriptMessageBus &instance();

    // Main thread only.
    void registerEngine(ScriptEngine *engine);
    void unregisterEngine(ScriptEngine *engine);
    void registerEndpoint(const QString &scope, const QString &filename,
                          ScriptContext *ctx, ScriptEngine *engine);
    void unregisterEndpoint(const QString &scope, const QString &filename);

    // Fire a script event at one scope's engine ("_global" or a bot name).
    // Main thread only (fireEvent walks the engine's script map).
    void fireEventForScope(const QString &scope, const QString &eventName,
                           const QVariantList &args);

    // Engine for a scope, or null. Main thread only.
    ScriptEngine *engineForScope(const QString &scope);

    // Any thread; GIL must not be held. Both return the number of scripts the
    // message was handed to (handler dispatched or inbox enqueued) at send
    // time; delivery itself is asynchronous.
    int publish(const QString &topic, const QVariant &data,
                const QString &senderScope, const QString &senderScript);
    int sendTo(const QString &targetScope, const QString &topic, const QVariant &data,
               const QString &senderScope, const QString &senderScript);

    // Topic interest for one script. An empty topic just opens the mailbox
    // (reachable by sendTo without subscribing to any topic). Returns false if
    // the script is not a registered endpoint.
    bool subscribe(const QString &scope, const QString &script, const QString &topic);
    bool unsubscribe(const QString &scope, const QString &script, const QString &topic);

    // Mark the script's mailbox open and return its context for comms.receive,
    // or null if the script is not a registered endpoint.
    ScriptContext *openMailbox(const QString &scope, const QString &script);
    int pendingCount(const QString &scope, const QString &script);

private:
    explicit ScriptMessageBus(QObject *parent = nullptr);

    struct Endpoint {
        QString scope;
        QString filename;
        ScriptContext *ctx = nullptr;
        ScriptEngine *engine = nullptr;
        QSet<QString> topics;
        bool mailboxOpen = false;
    };

    static QString endpointKey(const QString &scope, const QString &filename);
    static bool hasMessageHandler(const ScriptContext *ctx);
    static QVariantMap buildMessage(const QString &topic, const QVariant &data,
                                    const QString &senderScope, const QString &senderScript);
    void deliver(const Endpoint &ep, bool viaHandler, const QVariantMap &message);

    QMutex m_mutex;
    QHash<QString, ScriptEngine*> m_engines; // scope -> engine
    QHash<QString, Endpoint> m_endpoints;    // endpointKey() -> endpoint
};

#endif // SCRIPTMESSAGEBUS_H
