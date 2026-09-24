#ifndef SECTIONDIRTYTRACKER_H
#define SECTIONDIRTYTRACKER_H

#include <QAtomicInteger>
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QVector>
#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

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
// polls cannot be listed - its content is gone from memory - so dropColumn moves
// it to a bounded log of dropped (section, sequence) pairs instead, and a poll
// reports the ones marked after its token: a caller that falls behind learns
// exactly which sections it missed. Reloading the column marks every section
// again, so the caller converges as soon as the data is back.
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
        // Dropped sections after `since` may be missing from the list: the log no longer
        // reaches back that far, or `since` is not a token this tracker can place.
        bool droppedIncomplete = false;
    };

    // A section that was marked and then unloaded before it could be listed.
    struct DroppedSection {
        SectionKey key;
        QByteArray dimension;  // UTF-8; empty when not known
        bool remarked = false;  // tracked again since, so its column may have come back
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

    // The contents are gone from memory, so the column's sections move to the dropped
    // log; the consumer's own cache is what prevents a re-upload when it comes back.
    // `dimension` is the unloaded column's, so a dimension-filtered poll only hears
    // about its own dimension.
    void dropColumn(qint32 chunkX, qint32 chunkZ, const QByteArray &dimension)
    {
        QMutexLocker locker(&mutex);
        auto columnIt = byColumn.find({chunkX, chunkZ});
        if (columnIt == byColumn.end()) {
            return;
        }
        const quint16 dim = internDimension(dimension);
        for (auto it = columnIt->cbegin(); it != columnIt->cend(); ++it) {
            bySeq.remove(it.value());
            droppedLog.push_back({it.value(), {chunkX, chunkZ, it.key()}, dim});
        }
        byColumn.erase(columnIt);
        trimDroppedLog();
    }

    // Drops every tracked column, as if each had unloaded. `dimensionOf` names a
    // column's dimension; without it (or where it returns empty) the dimension is
    // unknown, which every dimension filter reports.
    void clear(const std::function<QByteArray(qint32 chunkX, qint32 chunkZ)> &dimensionOf = {})
    {
        QMutexLocker locker(&mutex);
        std::vector<DroppedEntry> entries;
        entries.reserve(static_cast<size_t>(bySeq.size()));
        for (auto columnIt = byColumn.cbegin(); columnIt != byColumn.cend(); ++columnIt) {
            const SectionColumnKey column = columnIt.key();
            const quint16 dim = dimensionOf ? internDimension(dimensionOf(column.chunkX, column.chunkZ)) : 0;
            for (auto it = columnIt->cbegin(); it != columnIt->cend(); ++it) {
                entries.push_back({it.value(), {column.chunkX, column.chunkZ, it.key()}, dim});
            }
        }
        // Oldest mark first, so if this overflows the log it is the oldest marks that fall
        // out - the ones a caller is least likely still to be owed.
        std::sort(entries.begin(), entries.end(),
                  [](const DroppedEntry &a, const DroppedEntry &b) { return a.seq < b.seq; });
        droppedLog.insert(droppedLog.end(), entries.cbegin(), entries.cend());
        byColumn.clear();
        bySeq.clear();
        trimDroppedLog();
    }

    // Keys marked after `since`; no value (or a token this tracker never issued)
    // means everything currently tracked. `limit` of 0 is unlimited; otherwise at
    // most that many keys are appended and the returned token resumes exactly
    // where this call stopped. Keys come back in the order they were marked.
    //
    // `dropped` receives the dropped-log entries this call covers: marked after
    // `since`, at or before the returned token, in the order they were dropped. A
    // key dropped, reloaded and dropped again can appear more than once. Nothing
    // is reported without a `since` - a caller with no token has nothing to have
    // missed - and an unplaceable `since` reports nothing but sets
    // droppedIncomplete.
    Snapshot snapshot(std::optional<quint64> since, int limit, QVector<SectionKey> &out,
                      QVector<DroppedSection> *dropped = nullptr) const
    {
        QMutexLocker locker(&mutex);

        quint64 from = 0;
        bool placed = false;
        if (since && (*since >> kSeqBits) == epoch) {
            const quint64 seen = *since & kSeqMask;
            // A token from the future (never issued by this tracker) degrades to a
            // full snapshot rather than reporting nothing forever.
            placed = seen <= seq;
            from = placed ? seen : 0;
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

        const quint64 to = result.truncated ? highWater : seq;
        result.token = makeToken(to);

        if (dropped && since) {
            if (!placed) {
                result.droppedIncomplete = true;
            } else {
                result.droppedIncomplete = from < droppedFloor;
                for (const DroppedEntry &entry : droppedLog) {
                    if (entry.seq > from && entry.seq <= to) {
                        dropped->append({entry.key, dimensions[entry.dimension], isTracked(entry.key)});
                    }
                }
            }
        }
        return result;
    }

private:
    // 24 bits of instance epoch over 40 bits of sequence: 16M trackers per process
    // and 1.1e12 marks per tracker, both far beyond anything a session reaches.
    static constexpr int kSeqBits = 40;
    static constexpr quint64 kSeqMask = (quint64(1) << kSeqBits) - 1;
    static constexpr quint64 kEpochMask = (quint64(1) << (64 - kSeqBits)) - 1;

    // A flying bot drops about as many sections as it loads, 1,000-2,000 a second at
    // view distance 8-12. Evicting the oldest drops first, this keeps every drop of the
    // last ~16 s at that rate, far longer than a poller that must keep up can let its
    // token age. 24 bytes an entry, so under 1 MB per bot when full.
    static constexpr size_t kDroppedLogCap = 32768;

    struct DroppedEntry {
        quint64 seq;
        SectionKey key;
        quint16 dimension;  // index into `dimensions`
    };

    static QAtomicInteger<quint32> &epochCounter()
    {
        static QAtomicInteger<quint32> counter{0};
        return counter;
    }

    quint64 makeToken(quint64 sequence) const
    {
        return (epoch << kSeqBits) | (sequence & kSeqMask);
    }

    bool isTracked(const SectionKey &key) const
    {
        auto columnIt = byColumn.constFind({key.chunkX, key.chunkZ});
        return columnIt != byColumn.cend() && columnIt->contains(key.sectionY);
    }

    quint16 internDimension(const QByteArray &dimension)
    {
        const qsizetype index = dimensions.indexOf(dimension);
        if (index >= 0) {
            return static_cast<quint16>(index);
        }
        // A world has a handful of dimensions; any past what the index holds read as unknown.
        if (dimensions.size() > 0xffff) {
            return 0;
        }
        dimensions.append(dimension);
        return static_cast<quint16>(dimensions.size() - 1);
    }

    void trimDroppedLog()
    {
        while (droppedLog.size() > kDroppedLogCap) {
            droppedFloor = std::max(droppedFloor, droppedLog.front().seq);
            droppedLog.pop_front();
        }
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
    // Unloaded sections in the order they dropped, each under the sequence it was last
    // marked with. A poll scans it whole; at the cap that is well under 100 us.
    std::deque<DroppedEntry> droppedLog;
    // Highest sequence evicted from droppedLog: a token older than this may have missed
    // drops the log no longer holds.
    quint64 droppedFloor = 0;
    // Index 0 is the empty "unknown" dimension.
    QVector<QByteArray> dimensions{QByteArray()};
};

#endif // SECTIONDIRTYTRACKER_H
