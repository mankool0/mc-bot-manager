#ifndef SECTIONDIRTYTRACKER_H
#define SECTIONDIRTYTRACKER_H

#include <QAtomicInteger>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QVector>
#include <optional>

struct SectionKey {
    qint32 chunkX = 0;
    qint32 chunkZ = 0;
    qint32 sectionY = 0;

    bool operator==(const SectionKey &other) const {
        return chunkX == other.chunkX && chunkZ == other.chunkZ && sectionY == other.sectionY;
    }
};

inline size_t qHash(const SectionKey &key, size_t seed = 0)
{
    return qHashMulti(seed, key.chunkX, key.chunkZ, key.sectionY);
}

struct SectionColumnKey {
    qint32 chunkX = 0;
    qint32 chunkZ = 0;

    bool operator==(const SectionColumnKey &other) const {
        return chunkX == other.chunkX && chunkZ == other.chunkZ;
    }
};

inline size_t qHash(const SectionColumnKey &key, size_t seed = 0)
{
    return qHashMulti(seed, key.chunkX, key.chunkZ);
}

// Per-bot record of which chunk sections changed, for world.changed_sections().
// Marked on the main thread from BotManager::markWorldDirty, snapshotted from
// script threads. Only (section, sequence) pairs are stored, never content, so a
// caller that hashes afterwards can over-report but never miss a change to a
// section that is still loaded. A section whose column unloads before the caller
// polls is dropped rather than reported (see dropColumn) - its content is gone
// from memory, so there is nothing left to hand out. Reloading the column marks
// every section again, so the caller converges as soon as the data is back.
//
// Any number of consumers can poll independently: a token is just a watermark to
// compare against, so reading never consumes and one slow consumer cannot starve
// another. This is deliberately not the same mechanism as WorldAutoSaver's
// m_dirtyBlockChunks, which is a single-consumer drain-and-clear set at column
// granularity; both are fed from markWorldDirty.
//
// Tokens pack a per-instance epoch above the sequence so a token from another
// bot's tracker (or from a bot that was removed and re-added) degrades to a full
// snapshot instead of silently under-reporting. The sequence itself is monotonic
// for the life of the tracker and survives clear(), so a token issued before a
// disconnect stays valid across the reconnect.
class SectionDirtyTracker
{
public:
    struct Snapshot {
        quint64 token = 0;
        bool truncated = false;  // limit was hit; more sections are pending at this token
    };

    SectionDirtyTracker()
        : epoch((epochCounter().fetchAndAddRelaxed(1) + 1) & kEpochMask)
    {
    }

    void mark(qint32 chunkX, qint32 chunkZ, qint32 sectionY)
    {
        QMutexLocker locker(&mutex);
        markLocked({chunkX, chunkZ, sectionY});
    }

    void markAll(const QVector<SectionKey> &keys)
    {
        if (keys.isEmpty()) {
            return;
        }
        QMutexLocker locker(&mutex);
        for (const SectionKey &key : keys) {
            markLocked(key);
        }
    }

    // The contents are gone from memory, so there is nothing left to report;
    // the consumer's own cache is what prevents a re-upload when it comes back.
    void dropColumn(qint32 chunkX, qint32 chunkZ)
    {
        QMutexLocker locker(&mutex);
        auto columnIt = byColumn.find({chunkX, chunkZ});
        if (columnIt == byColumn.end()) {
            return;
        }
        for (auto it = columnIt->cbegin(); it != columnIt->cend(); ++it) {
            bySeq.remove(it.value());
        }
        byColumn.erase(columnIt);
    }

    void clear()
    {
        QMutexLocker locker(&mutex);
        byColumn.clear();
        bySeq.clear();
    }

    // Keys marked after `since`; no value (or a token this tracker never issued)
    // means everything currently tracked. `limit` of 0 is unlimited; otherwise at
    // most that many keys are appended and the returned token resumes exactly
    // where this call stopped. Keys come back in the order they were marked.
    Snapshot snapshot(std::optional<quint64> since, int limit, QVector<SectionKey> &out) const
    {
        QMutexLocker locker(&mutex);

        quint64 from = 0;
        if (since && (*since >> kSeqBits) == epoch) {
            const quint64 seen = *since & kSeqMask;
            // A token from the future (never issued by this tracker) degrades to a
            // full snapshot rather than reporting nothing forever.
            from = seen > seq ? 0 : seen;
        }

        Snapshot result;
        quint64 highWater = from;
        int emitted = 0;
        for (auto it = bySeq.upperBound(from); it != bySeq.cend(); ++it) {
            if (limit > 0 && emitted >= limit) {
                result.truncated = true;
                break;
            }
            out.append(it.value());
            highWater = it.key();
            ++emitted;
        }

        result.token = makeToken(result.truncated ? highWater : seq);
        return result;
    }

private:
    // 24 bits of instance epoch over 40 bits of sequence: 16M trackers per process
    // and 1.1e12 marks per tracker, both far beyond anything a session reaches.
    static constexpr int kSeqBits = 40;
    static constexpr quint64 kSeqMask = (quint64(1) << kSeqBits) - 1;
    static constexpr quint64 kEpochMask = (quint64(1) << (64 - kSeqBits)) - 1;

    static QAtomicInteger<quint32> &epochCounter()
    {
        static QAtomicInteger<quint32> counter{0};
        return counter;
    }

    quint64 makeToken(quint64 sequence) const
    {
        return (epoch << kSeqBits) | (sequence & kSeqMask);
    }

    void markLocked(const SectionKey &key)
    {
        QHash<qint32, quint64> &sections = byColumn[{key.chunkX, key.chunkZ}];
        auto it = sections.find(key.sectionY);
        if (it != sections.end()) {
            bySeq.remove(it.value());
            *it = ++seq;
        } else {
            sections.insert(key.sectionY, ++seq);
        }
        bySeq.insert(seq, key);
    }

    mutable QMutex mutex;
    quint64 seq = 0;
    const quint64 epoch;
    // Nested by column so an unload is O(sections in the column) rather than a
    // scan of every tracked section; ordered by sequence so a poll is O(delta).
    QHash<SectionColumnKey, QHash<qint32, quint64>> byColumn;
    QMap<quint64, SectionKey> bySeq;
};

#endif // SECTIONDIRTYTRACKER_H
