#ifndef COLUMNEXTRASOBSERVATION_H
#define COLUMNEXTRASOBSERVATION_H

#include "bot/SectionDirtyTracker.h"
#include "world/ColumnExtrasCodec.h"

#include <QByteArray>
#include <QVector>
#include <optional>

class BotWorldData;
class QReadWriteLock;
class RetainedExtras;

// The work behind world.changed_extras / world.export_extras / world.get_extras: the
// SectionObservation of a column's biomes and block entities. The tracker is an ordinary
// SectionDirtyTracker marked with section_y 0 for the whole column, so tokens, dropped logs
// and retention behave exactly as they do for sections.
namespace ColumnExtrasObservation {

constexpr int kMaxDroppedKeys = 4096;

struct Change {
    SectionColumnKey key;
    QByteArray digest;  // empty unless a digest was asked for
};

struct Changes {
    quint64 token = 0;
    bool truncated = false;
    QVector<Change> columns;
    QVector<SectionColumnKey> dropped;
    qsizetype droppedTotal = 0;
    bool droppedIncomplete = false;
};

Changes listChanges(const SectionDirtyTracker &tracker, const BotWorldData &world, QReadWriteLock &worldLock,
                    std::optional<quint64> since, const QByteArray &dimension, bool digest, int limit,
                    const QByteArray &digestPrefix, const RetainedExtras *retained = nullptr);

QByteArray exportColumns(const BotWorldData &world, QReadWriteLock &worldLock, const QVector<SectionColumnKey> &keys,
                         const QByteArray &dimension, const RetainedExtras *retained = nullptr);

struct Column {
    QByteArray dimension;
    ColumnExtrasCodec::CanonicalColumn canonical;
};

std::optional<Column> readColumn(const BotWorldData &world, QReadWriteLock &worldLock, const SectionColumnKey &key,
                                 const QByteArray &dimension, const RetainedExtras *retained = nullptr);

// For BotManager::unloadColumn, under the world write lock and before the tracker drops the
// column: a column still marked keeps its extras for a while.
void retainOnUnload(const SectionDirtyTracker &tracker, const BotWorldData &world, RetainedExtras &retained,
                    qint32 chunkX, qint32 chunkZ, qint64 now);

}

#endif // COLUMNEXTRASOBSERVATION_H
