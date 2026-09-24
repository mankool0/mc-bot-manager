#include "SectionObservation.h"
#include "bot/WorldData.h"

#include <QReadWriteLock>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>
#include <cstring>

namespace SectionObservation {

namespace {

// Its own pool rather than the global one, which UI work shares. The calling thread works
// through its batch as well, so ten callers at once slow each other down but never wait on
// an idle pool.
QThreadPool &workerPool()
{
    static QThreadPool pool;
    return pool;
}

// Below this many jobs, handing them out costs about what doing them does.
constexpr qsizetype kMinParallelJobs = 32;

template <typename Job, typename Fn>
void runJobs(QVector<Job> &jobs, Fn &&fn)
{
    if (jobs.size() < kMinParallelJobs) {
        for (Job &job : jobs) {
            fn(job);
        }
        return;
    }
    QtConcurrent::blockingMap(&workerPool(), jobs, fn);
}

// Shallow copies: Qt's implicit sharing makes each ChunkSection copy O(1), so the read lock is
// held only for lookups and the hashing runs after it is released.
struct DigestJob {
    SectionKey key;
    ChunkSection section;
    QByteArray digest;
    bool encoded = false;
};

struct ExportJob {
    SectionKey key;
    QByteArray dimension;
    ChunkSection section;
    qsizetype slot = 0;       // offset of the frame in the payload, sized for its largest encoding
    qsizetype frameSize = 0;  // 0 when the section could not be encoded
};

// The chunk's dimension in UTF-8, converted once per column rather than once per section:
// keys arrive column by column.
struct DimensionCache {
    const ChunkData *chunk = nullptr;
    QByteArray dimension;

    const QByteArray &of(const ChunkData *c)
    {
        if (c != chunk) {
            chunk = c;
            dimension = c->dimension.toUtf8();
        }
        return dimension;
    }
};

}

Changes listChanges(const SectionDirtyTracker &tracker, const BotWorldData &world, QReadWriteLock &worldLock,
                    std::optional<quint64> since, const QByteArray &dimension, bool digest, int limit,
                    const QByteArray &digestPrefix)
{
    Changes result;
    QVector<SectionKey> keys;
    QVector<SectionDirtyTracker::DroppedSection> dropped;
    QVector<DigestJob> jobs;
    QVector<SectionKey> lost;
    {
        // The tracker is read under the world lock, and BotManager drops an unloading column
        // from the tracker under the write lock, so the two agree: a listed key's column is
        // loaded, and an unloaded one is in `dropped`. A missing chunk is still handled, as a
        // loss, rather than trusted never to happen.
        QReadLocker locker(&worldLock);
        const SectionDirtyTracker::Snapshot snap = tracker.snapshot(since, limit, keys, &dropped);
        result.token = snap.token;
        result.truncated = snap.truncated;
        result.droppedIncomplete = snap.droppedIncomplete;

        DimensionCache dims;
        jobs.reserve(keys.size());
        for (const SectionKey &key : std::as_const(keys)) {
            const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
            if (!chunk) {
                lost.append(key);
                continue;
            }
            if (!dimension.isEmpty() && dims.of(chunk) != dimension) {
                continue;
            }
            auto it = chunk->sections.constFind(key.sectionY);
            // A column reloaded with fewer sections than it had: the old one is gone.
            if (it == chunk->sections.constEnd()) {
                lost.append(key);
                continue;
            }
            jobs.append({key, *it, {}, false});
        }

        for (const SectionDirtyTracker::DroppedSection &d : std::as_const(dropped)) {
            if (!dimension.isEmpty() && !d.dimension.isEmpty() && d.dimension != dimension) {
                continue;
            }
            // Its column came back and was marked again, so the new content reaches this caller
            // through `sections`, now or on a later poll - unless what came back is another
            // dimension's column at the same coordinates.
            if (d.remarked) {
                const ChunkData *chunk = world.getChunk(d.key.chunkX, d.key.chunkZ);
                if (chunk && chunk->sections.contains(d.key.sectionY)
                    && (d.dimension.isEmpty() || dims.of(chunk) == d.dimension)) {
                    continue;
                }
            }
            lost.append(d.key);
        }
    }

    if (digest) {
        runJobs(jobs, [&digestPrefix](DigestJob &job) {
            std::optional<QByteArray> d = SectionCodec::sectionDigest(job.section, digestPrefix);
            if (d) {
                job.digest = std::move(*d);
                job.encoded = true;
            }
        });
    }

    result.sections.reserve(jobs.size());
    for (DigestJob &job : jobs) {
        if (digest && !job.encoded) {
            lost.append(job.key);
            continue;
        }
        result.sections.append({job.key, std::move(job.digest)});
    }

    QSet<SectionKey> seen;
    seen.reserve(lost.size());
    for (const SectionKey &key : std::as_const(lost)) {
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        if (result.dropped.size() < kMaxDroppedKeys) {
            result.dropped.append(key);
        }
    }
    result.droppedTotal = seen.size();
    return result;
}

QByteArray exportSections(const BotWorldData &world, QReadWriteLock &worldLock, const QVector<SectionKey> &keys,
                          const QByteArray &dimension)
{
    QVector<ExportJob> jobs;
    jobs.reserve(keys.size());
    {
        QReadLocker locker(&worldLock);
        DimensionCache dims;
        for (const SectionKey &key : keys) {
            const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
            if (!chunk) {
                continue;
            }
            const QByteArray &dim = dims.of(chunk);
            if (!dimension.isEmpty() && dim != dimension) {
                continue;
            }
            auto it = chunk->sections.constFind(key.sectionY);
            if (it == chunk->sections.constEnd()) {
                continue;
            }
            jobs.append({key, dim, *it});
        }
    }

    // Laid out before anything is encoded: each frame gets a slot sized for its worst case, the
    // workers write straight into it, and the slack is closed afterwards. Nothing a worker
    // allocates outlives its job, so no blob is freed on a thread other than the one that made
    // it - glibc serializes those frees on the owning thread's arena, which under ten callers
    // cost more than the parallelism gained.
    qsizetype layout = 4;
    for (ExportJob &job : jobs) {
        job.slot = layout;
        layout += SectionCodec::exportFrameHeaderSize(job.dimension) + SectionCodec::maxBlobSize(job.section);
    }
    QByteArray payload(layout, Qt::Uninitialized);
    char *base = payload.data();

    runJobs(jobs, [base](ExportJob &job) {
        auto canon = SectionCodec::canonicalize(job.section);
        if (!canon) {
            return;
        }
        char *start = base + job.slot;
        char *p = SectionCodec::writeExportFrameHeader(start, job.dimension, job.key.chunkX, job.key.chunkZ,
                                                       job.key.sectionY,
                                                       static_cast<quint32>(SectionCodec::blobSize(*canon)));
        p = SectionCodec::writeBlob(p, *canon);
        job.frameSize = p - start;
    });

    qsizetype end = 4;
    quint32 frames = 0;
    for (const ExportJob &job : std::as_const(jobs)) {
        if (job.frameSize == 0) {
            continue;
        }
        if (job.slot != end) {
            memmove(base + end, base + job.slot, static_cast<size_t>(job.frameSize));
        }
        end += job.frameSize;
        ++frames;
    }
    SectionCodec::writeExportCount(base, frames);
    payload.truncate(end);
    return payload;
}

std::optional<Section> readSection(const BotWorldData &world, QReadWriteLock &worldLock, const SectionKey &key,
                                   const QByteArray &dimension)
{
    ChunkSection section;
    QByteArray sectionDimension;
    {
        QReadLocker locker(&worldLock);
        const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
        if (!chunk) {
            return std::nullopt;
        }
        sectionDimension = chunk->dimension.toUtf8();
        if (!dimension.isEmpty() && sectionDimension != dimension) {
            return std::nullopt;
        }
        auto it = chunk->sections.constFind(key.sectionY);
        if (it == chunk->sections.constEnd()) {
            return std::nullopt;
        }
        section = *it;
    }
    auto canon = SectionCodec::canonicalize(section);
    if (!canon) {
        return std::nullopt;
    }
    return Section{sectionDimension, std::move(*canon)};
}

}
