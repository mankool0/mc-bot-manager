#include "ScriptMessageBus.h"
#include "ScriptContext.h"
#include "ScriptEngine.h"
#include "ScriptEventWorker.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QReadLocker>

ScriptMessageBus::ScriptMessageBus(QObject *parent)
    : QObject(parent)
{
}

ScriptMessageBus &ScriptMessageBus::instance()
{
    static ScriptMessageBus inst;
    return inst;
}

QString ScriptMessageBus::endpointKey(const QString &scope, const QString &filename)
{
    return scope + QLatin1Char('/') + filename;
}

void ScriptMessageBus::registerEngine(ScriptEngine *engine)
{
    QMutexLocker locker(&m_mutex);
    m_engines[engine->getScopeName()] = engine;
}

void ScriptMessageBus::unregisterEngine(ScriptEngine *engine)
{
    QMutexLocker locker(&m_mutex);
    const QString scope = engine->getScopeName();
    if (m_engines.value(scope) == engine)
        m_engines.remove(scope);
    for (auto it = m_endpoints.begin(); it != m_endpoints.end();) {
        if (it->engine == engine)
            it = m_endpoints.erase(it);
        else
            ++it;
    }
}

void ScriptMessageBus::registerEndpoint(const QString &scope, const QString &filename,
                                        ScriptContext *ctx, ScriptEngine *engine)
{
    QMutexLocker locker(&m_mutex);
    Endpoint ep;
    ep.scope = scope;
    ep.filename = filename;
    ep.ctx = ctx;
    ep.engine = engine;
    // Replaces any previous registration: subscriptions do not survive a re-run.
    m_endpoints[endpointKey(scope, filename)] = ep;
}

void ScriptMessageBus::unregisterEndpoint(const QString &scope, const QString &filename)
{
    QMutexLocker locker(&m_mutex);
    m_endpoints.remove(endpointKey(scope, filename));
}

void ScriptMessageBus::fireEventForScope(const QString &scope, const QString &eventName,
                                         const QVariantList &args)
{
    ScriptEngine *engine = engineForScope(scope);
    if (engine)
        engine->fireEvent(eventName, args);
}

ScriptEngine *ScriptMessageBus::engineForScope(const QString &scope)
{
    QMutexLocker locker(&m_mutex);
    return m_engines.value(scope, nullptr);
}

bool ScriptMessageBus::hasMessageHandler(const ScriptContext *ctx)
{
    QReadLocker locker(&ctx->handlersLock);
    return ctx->eventHandlers.contains(QStringLiteral("script_message"));
}

QVariantMap ScriptMessageBus::buildMessage(const QString &topic, const QVariant &data,
                                           const QString &senderScope, const QString &senderScript)
{
    QVariantMap message;
    message[QStringLiteral("topic")] = topic;
    message[QStringLiteral("data")] = data;
    message[QStringLiteral("sender_scope")] = senderScope;
    message[QStringLiteral("sender_script")] = senderScript;
    message[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    return message;
}

int ScriptMessageBus::publish(const QString &topic, const QVariant &data,
                              const QString &senderScope, const QString &senderScript)
{
    const QVariantMap message = buildMessage(topic, data, senderScope, senderScript);

    int count = 0;
    QMutexLocker locker(&m_mutex);
    for (const Endpoint &ep : std::as_const(m_endpoints)) {
        if (ep.scope == senderScope && ep.filename == senderScript)
            continue;
        if (!ep.topics.contains(topic))
            continue;
        deliver(ep, hasMessageHandler(ep.ctx), message);
        count++;
    }
    return count;
}

int ScriptMessageBus::sendTo(const QString &targetScope, const QString &topic, const QVariant &data,
                             const QString &senderScope, const QString &senderScript)
{
    const QVariantMap message = buildMessage(topic, data, senderScope, senderScript);

    int count = 0;
    QMutexLocker locker(&m_mutex);
    for (const Endpoint &ep : std::as_const(m_endpoints)) {
        if (ep.scope != targetScope)
            continue;
        if (ep.scope == senderScope && ep.filename == senderScript)
            continue;
        const bool viaHandler = hasMessageHandler(ep.ctx);
        // A script is addressable once it opted in to messaging in any form:
        // a script_message handler, any subscribe() call, or a receive() call.
        if (!viaHandler && !ep.mailboxOpen && ep.topics.isEmpty())
            continue;
        deliver(ep, viaHandler, message);
        count++;
    }
    return count;
}

void ScriptMessageBus::deliver(const Endpoint &ep, bool viaHandler, const QVariantMap &message)
{
    if (viaHandler) {
        ScriptEvent event;
        event.scriptFilename = ep.filename;
        event.eventName = QStringLiteral("script_message");
        event.args = QVariantList{message};
        event.botName = ep.scope == QLatin1String("_global") ? QString() : ep.scope;
        ep.engine->postEvent(event, ep.ctx);
        return;
    }

    QMutexLocker locker(&ep.ctx->inboxMutex);
    if (ep.ctx->inbox.size() >= ScriptContext::kMaxInbox) {
        ep.ctx->inbox.dequeue();
        if (ep.ctx->droppedMessages++ == 0) {
            qWarning() << QString("[%1] comms inbox of %2 is full - dropping oldest message(s)")
                              .arg(ep.scope, ep.filename);
        }
    }
    ep.ctx->inbox.enqueue(message);
    ep.ctx->inboxCond.wakeOne();
}

bool ScriptMessageBus::subscribe(const QString &scope, const QString &script, const QString &topic)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_endpoints.find(endpointKey(scope, script));
    if (it == m_endpoints.end())
        return false;
    if (topic.isEmpty())
        it->mailboxOpen = true;
    else
        it->topics.insert(topic);
    return true;
}

bool ScriptMessageBus::unsubscribe(const QString &scope, const QString &script, const QString &topic)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_endpoints.find(endpointKey(scope, script));
    if (it == m_endpoints.end())
        return false;
    if (topic.isEmpty())
        it->mailboxOpen = false;
    else
        it->topics.remove(topic);
    return true;
}

ScriptContext *ScriptMessageBus::openMailbox(const QString &scope, const QString &script)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_endpoints.find(endpointKey(scope, script));
    if (it == m_endpoints.end())
        return nullptr;
    it->mailboxOpen = true;
    return it->ctx;
}

int ScriptMessageBus::pendingCount(const QString &scope, const QString &script)
{
    ScriptContext *ctx = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_endpoints.find(endpointKey(scope, script));
        if (it == m_endpoints.end())
            return 0;
        ctx = it->ctx;
    }
    QMutexLocker locker(&ctx->inboxMutex);
    return ctx->inbox.size();
}
