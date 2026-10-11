#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>
#include <deque>
#include <memory>
#include <vector>
#include "bot/WorldData.h"
#include "saving/DimChunkPos.h"

class RegionFile;

class ChunkSavingWorker : public QObject {
    Q_OBJECT

public:
    explicit ChunkSavingWorker(const QString& worldPath, int dataVersion, QObject *parent = nullptr);
    ~ChunkSavingWorker();

    // Callable from any thread. A chunk still waiting at the same position is replaced rather
    // than queued again: bots sharing a world reload the same columns over and over, and a
    // copy per reload is what let the backlog outgrow the writer.
    void enqueueChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities);
    int pendingChunkCount() const;

    // Writes each chunk's entities over their copies on disk, keeping the rest of what the chunk
    // holds, and drops the copies of entities that moved elsewhere or are in `dead`.
    void processEntityFlush(const QHash<DimChunkPos, QVector<EntityData>>& chunks, const QSet<QString>& dead);

public slots:
    void processPlayerData(const PlayerSaveData& data, const QString& worldPath, int dataVersion);

private slots:
    void savePendingChunk();

private:
    struct PendingChunk {
        ChunkData chunk;
        QVector<BlockEntityData> blockEntities;
    };

    struct EntityChunkRef {
        qint32 chunkX = 0, chunkZ = 0;
        quint8 dimension = 0;
        bool operator==(const EntityChunkRef& other) const {
            return chunkX == other.chunkX && chunkZ == other.chunkZ && dimension == other.dimension;
        }
    };

    void processChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities);
    RegionFile* openRegion(const QString& path);
    void closeRegion(const QString& path);
    void rewriteEntityChunk(const DimChunkPos& pos, const QVector<EntityData>& entities, const QSet<QUuid>& dead);
    void setEntityLocation(const QUuid& uuid, const EntityChunkRef* where);
    void loadEntityIndex();
    void saveEntityIndex();
    void compactEntityIndex();

    QString m_worldPath;
    int m_dataVersion;

    mutable QMutex m_pendingMutex;
    QHash<DimChunkPos, PendingChunk> m_pending;
    std::deque<DimChunkPos> m_pendingOrder;
    // One wake-up event at a time: set by the producer that found the queue empty, cleared
    // by the drain that empties it, so the event queue never holds more than one.
    bool m_drainScheduled = false;
    QElapsedTimer m_backlogWarning;

    // Most recently used last. Only this thread writes these files, and closing after every
    // chunk cost an open, an 8 KB header read and a sector-map rebuild per write.
    std::vector<std::pair<QString, std::unique_ptr<RegionFile>>> m_openRegions;

    // The chunk holding each saved entity's copy, kept on disk so a later session that sees it
    // move or die can find it. One entry per entity the save holds, hence the compact value.
    QHash<QUuid, EntityChunkRef> m_entityIndex;
    bool m_entityIndexLoaded = false;
    QByteArray m_entityIndexPending;
    qint64 m_entityIndexPendingRecords = 0;
    qint64 m_entityIndexFileRecords = 0;
};
