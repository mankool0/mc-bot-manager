#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QElapsedTimer>
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

public slots:
    void processEntityChunk(int chunkX, int chunkZ, const QString& dimension,
                            const QVector<EntityData>& entities,
                            const QString& worldPath, int dataVersion);
    void processPlayerData(const PlayerSaveData& data, const QString& worldPath, int dataVersion);

private slots:
    void savePendingChunk();

private:
    struct PendingChunk {
        ChunkData chunk;
        QVector<BlockEntityData> blockEntities;
    };

    void processChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities);
    RegionFile* openRegion(const QString& path);

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
};
