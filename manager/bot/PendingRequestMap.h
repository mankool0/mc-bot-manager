#ifndef PENDINGREQUESTMAP_H
#define PENDINGREQUESTMAP_H

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QString>

// Correlates a blocking manager -> client request with the client's reply. The sender creates a
// Request keyed by the outbound envelope's message_id, sends, then blocks on wait(); the inbound
// handler calls complete() with the same id. T is the reply payload; use bool when the reply only
// needs to say "it arrived".
//
// Request owns its entry and deregisters in its destructor, so an early return on the send path
// cannot leave a dangling pointer in the map.
template <typename T>
class PendingRequestMap
{
public:
    class Request
    {
    public:
        Request(PendingRequestMap &map, const QString &id)
            : m_map(map), m_id(id)
        {
            QMutexLocker lock(&m_map.m_mutex);
            m_map.m_entries.insert(m_id, &m_entry);
        }

        ~Request()
        {
            QMutexLocker lock(&m_map.m_mutex);
            m_map.m_entries.remove(m_id);
        }

        Request(const Request &) = delete;
        Request &operator=(const Request &) = delete;

        // Blocks until the reply arrives or timeoutMs elapses. Returns true if it arrived.
        bool wait(int timeoutMs) { return m_entry.sem.tryAcquire(1, timeoutMs); }

        const T &value() const { return m_entry.value; }

    private:
        struct Entry {
            QSemaphore sem{0};
            T value{};
        };

        // Declared before m_map/m_id so it is destroyed last: the destructor body runs before
        // any member is destroyed, so the entry is out of the map by the time it dies.
        Entry m_entry;
        PendingRequestMap &m_map;
        QString m_id;

        friend class PendingRequestMap;
    };

    // Called from the inbound handler. Applies setter to the waiting entry's payload and wakes
    // the sender. Returns false when nobody is waiting, which is the normal case for a reply
    // that arrived after its requester already timed out.
    template <typename Setter>
    bool complete(const QString &requestId, Setter &&setter)
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_entries.find(requestId);
        if (it == m_entries.end())
            return false;
        setter((*it)->value);
        (*it)->sem.release();
        return true;
    }

    // Payload-less variant, for replies whose data lands somewhere else before the wake
    // (see how the statistics response merges into the stats cache).
    bool complete(const QString &requestId)
    {
        return complete(requestId, [](T &) {});
    }

private:
    QMutex m_mutex;
    QHash<QString, typename Request::Entry *> m_entries;
};

#endif // PENDINGREQUESTMAP_H
