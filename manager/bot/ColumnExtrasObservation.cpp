#include "ColumnExtrasObservation.h"
#include "bot/RetainedExtras.h"
#include "bot/WorldData.h"

#include <QReadWriteLock>
#include <QSet>

namespace ColumnExtrasObservation {

namespace {

// What a column holds, copied under the read lock and canonicalized after it is released.
// The section map copy is O(1) (implicit sharing); block entities are few per column.
struct Job {
    SectionColumnKey key;
    QByteArray dimension;
    QMap<int32_t, ChunkSection> sections;
    QVector<BlockEntityData> blockEntities;
    std::optional<ColumnExtrasCodec::CanonicalColumn> kept;  // from a retained copy
};

ColumnExtrasCodec::CanonicalColumn canonicalOf(const Job &job)
{
    if (job.kept) {
        return *job.kept;
    }
    return ColumnExtrasCodec::canonicalize(job.key.chunkX, job.key.chunkZ, job.sections, job.blockEntities);
}

// Block entities for every loaded job, one pass over the bot's block entities per dimension.
void attachBlockEntities(const BotWorldData &world, QVector<Job> &jobs)
{
    QHash<QByteArray, QSet<ChunkPos>> wanted;
    for (const Job &job : std::as_const(jobs)) {
        if (!job.kept) {
            wanted[job.dimension].insert(ChunkPos(job.key.chunkX, job.key.chunkZ));
        }
    }
    QHash<QByteArray, QHash<ChunkPos, QVector<BlockEntityData>>> found;
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        found.insert(it.key(), world.getBlockEntitiesInColumns(it.value(), QString::fromUtf8(it.key())));
    }
    for (Job &job : jobs) {
        if (!job.kept) {
            job.blockEntities = found.value(job.dimension).value(ChunkPos(job.key.chunkX, job.key.chunkZ));
        }
    }
}

}

Changes listChanges(const SectionDirtyTracker &tracker, const BotWorldData &world, QReadWriteLock &worldLock,
                    std::optional<quint64> since, const QByteArray &dimension, bool digest, int limit,
                    const QByteArray &digestPrefix, const RetainedExtras *retained)
{
    Changes result;
    QVector<SectionKey> keys;
    QVector<SectionDirtyTracker::DroppedSection> dropped;
    QVector<Job> jobs;
    QVector<SectionColumnKey> lost;
    {
        // As in SectionObservation::listChanges: the tracker and the world agree under this lock.
        QReadLocker locker(&worldLock);
        const SectionDirtyTracker::Snapshot snap = tracker.snapshot(since, limit, keys, &dropped);
        result.token = snap.token;
        result.truncated = snap.truncated;
        result.droppedIncomplete = snap.droppedIncomplete;

        const qint64 now = RetainedSections::nowMs();
        jobs.reserve(keys.size());
        for (const SectionKey &k : std::as_const(keys)) {
            const SectionColumnKey key{k.chunkX, k.chunkZ};
            const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
            if (!chunk) {
                lost.append(key);
                continue;
            }
            const QByteArray chunkDimension = chunk->dimension.toUtf8();
            if (!dimension.isEmpty() && chunkDimension != dimension) {
                continue;
            }
            jobs.append({key, chunkDimension, chunk->sections, {}, std::nullopt});
        }

        QSet<SectionColumnKey> fromCopies;
        for (const SectionDirtyTracker::DroppedSection &d : std::as_const(dropped)) {
            const SectionColumnKey key{d.key.chunkX, d.key.chunkZ};
            if (!dimension.isEmpty() && !d.dimension.isEmpty() && d.dimension != dimension) {
                continue;
            }
            if (d.remarked) {
                const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
                if (chunk && (d.dimension.isEmpty() || chunk->dimension.toUtf8() == d.dimension)) {
                    continue;
                }
            }
            const RetainedExtras::Entry *kept = retained ? retained->latest(key, now) : nullptr;
            if (kept && kept->seq >= d.seq
                && (d.dimension.isEmpty() || kept->dimension.isEmpty() || kept->dimension == d.dimension)) {
                if (!fromCopies.contains(key)) {
                    fromCopies.insert(key);
                    jobs.append({key, kept->dimension, {}, {}, kept->column});
                }
                continue;
            }
            lost.append(key);
        }
        if (digest) {
            attachBlockEntities(world, jobs);
        }
    }

    result.columns.reserve(jobs.size());
    for (const Job &job : std::as_const(jobs)) {
        Change change{job.key, {}};
        if (digest) {
            change.digest = ColumnExtrasCodec::digest(ColumnExtrasCodec::encodeBlob(canonicalOf(job)), digestPrefix);
        }
        result.columns.append(std::move(change));
    }

    QSet<SectionColumnKey> seen;
    for (const SectionColumnKey &key : std::as_const(lost)) {
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

QByteArray exportColumns(const BotWorldData &world, QReadWriteLock &worldLock, const QVector<SectionColumnKey> &keys,
                         const QByteArray &dimension, const RetainedExtras *retained)
{
    QVector<Job> jobs;
    jobs.reserve(keys.size());
    {
        QReadLocker locker(&worldLock);
        const qint64 now = RetainedSections::nowMs();
        for (const SectionColumnKey &key : keys) {
            const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
            if (chunk) {
                const QByteArray chunkDimension = chunk->dimension.toUtf8();
                if (dimension.isEmpty() || chunkDimension == dimension) {
                    jobs.append({key, chunkDimension, chunk->sections, {}, std::nullopt});
                    continue;
                }
            }
            const RetainedExtras::Entry *kept = retained ? retained->latest(key, now) : nullptr;
            if (kept && (dimension.isEmpty() || kept->dimension == dimension)) {
                jobs.append({key, kept->dimension, {}, {}, kept->column});
            }
        }
        attachBlockEntities(world, jobs);
    }

    QVector<ColumnExtrasCodec::ColumnFrame> frames;
    frames.reserve(jobs.size());
    for (const Job &job : std::as_const(jobs)) {
        frames.append({job.dimension, job.key.chunkX, job.key.chunkZ,
                       ColumnExtrasCodec::encodeBlob(canonicalOf(job))});
    }
    return ColumnExtrasCodec::encodeExport(frames);
}

std::optional<Column> readColumn(const BotWorldData &world, QReadWriteLock &worldLock, const SectionColumnKey &key,
                                 const QByteArray &dimension, const RetainedExtras *retained)
{
    QVector<Job> jobs;
    {
        QReadLocker locker(&worldLock);
        const ChunkData *chunk = world.getChunk(key.chunkX, key.chunkZ);
        const QByteArray chunkDimension = chunk ? chunk->dimension.toUtf8() : QByteArray();
        if (chunk && (dimension.isEmpty() || chunkDimension == dimension)) {
            jobs.append({key, chunkDimension, chunk->sections, {}, std::nullopt});
        } else {
            const RetainedExtras::Entry *kept = retained ? retained->latest(key, RetainedSections::nowMs()) : nullptr;
            if (!kept || (!dimension.isEmpty() && kept->dimension != dimension)) {
                return std::nullopt;
            }
            jobs.append({key, kept->dimension, {}, {}, kept->column});
        }
        attachBlockEntities(world, jobs);
    }
    return Column{jobs.first().dimension, canonicalOf(jobs.first())};
}

void retainOnUnload(const SectionDirtyTracker &tracker, const BotWorldData &world, RetainedExtras &retained,
                    qint32 chunkX, qint32 chunkZ, qint64 now)
{
    const auto marked = tracker.marked(chunkX, chunkZ);
    if (marked.empty()) {
        return;
    }
    const ChunkData *chunk = world.getChunk(chunkX, chunkZ);
    if (!chunk) {
        return;
    }
    quint64 seq = 0;
    for (const auto &[sectionY, s] : marked) {
        seq = std::max(seq, s);
    }
    retained.retain({chunkX, chunkZ}, seq, chunk->dimension.toUtf8(),
                    ColumnExtrasCodec::canonicalize(chunkX, chunkZ, chunk->sections,
                                                    world.getBlockEntitiesInChunk(chunkX, chunkZ, chunk->dimension)),
                    now);
}

}
