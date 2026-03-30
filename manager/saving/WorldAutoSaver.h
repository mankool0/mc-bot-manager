#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <functional>
#include <optional>
#include "bot/WorldData.h"
#include "world/WorldExporter.h"

struct DimChunkPos {
    QString dimension;
    int chunkX = 0, chunkZ = 0;

    bool operator==(const DimChunkPos& other) const {
        return chunkX == other.chunkX && chunkZ == other.chunkZ && dimension == other.dimension;
    }
};

inline size_t qHash(const DimChunkPos& pos, size_t seed = 0) {
    return qHashMulti(seed, pos.dimension, pos.chunkX, pos.chunkZ);
}

struct WorldSaveSettings {
    bool saveBlockEntities = true;
    bool saveEntities      = true;
    bool saveItemEntities  = true;
    bool savePlayerData    = true;
};

class ChunkSavingWorker;

class WorldAutoSaver : public QObject {
    Q_OBJECT

public:
    explicit WorldAutoSaver(const QString& serverIp, const MinecraftVersion& version,
                            const WorldSaveSettings& settings = {});
    ~WorldAutoSaver();

    // Called with (chunkX, chunkZ, dimension) -> chunk + block entities, or nullopt if not loaded
    using ChunkProvider = std::function<std::optional<std::pair<ChunkData, QVector<BlockEntityData>>>(int, int, const QString&)>;

    void saveChunkAsync(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities = {});
    void setChunkProvider(ChunkProvider provider);
    void markBlockChunkDirty(int chunkX, int chunkZ, const QString& dimension);
    void onEntitiesUpdated(const QVector<EntityData>& upserted, const QVector<int>& removed,
                           const QString& dimension);
    void setPlayerData(const PlayerSaveData& data);
    void flushPlayerData();
    void flushAll();  // Flush all dirty chunks + entities + player data; call before clearing world data

    int getDataVersion() const { return m_version.dataVersion; }
    QString getWorldPath() const { return m_worldPath; }
    const WorldSaveSettings& getSaveSettings() const { return m_saveSettings; }

signals:
    void chunkReadyForSaving(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities,
                             const QString& worldPath, int dataVersion);
    void entityChunkReadyForSaving(int chunkX, int chunkZ, const QString& dimension,
                                   const QVector<EntityData>& entities,
                                   const QString& worldPath, int dataVersion);
    void playerDataReadyForSaving(const PlayerSaveData& data, const QString& worldPath, int dataVersion);

private slots:
    void flushPeriodic();

private:
    void initializeWorld();

    QString m_worldPath;
    QString m_serverIp;
    MinecraftVersion m_version;
    WorldSaveSettings m_saveSettings;
    bool m_isInitialized;

    QThread* m_workerThread;
    ChunkSavingWorker* m_worker;

    // Entity tracking for periodic save
    struct TrackedEntity {
        EntityData data;
        QString dimension;
    };
    QHash<int, TrackedEntity> m_trackedEntities;
    QSet<DimChunkPos> m_dirtyEntityChunks;
    QTimer* m_periodicFlushTimer;

    // Player data (per-UUID to support multiple bots on the same server)
    QHash<QString, PlayerSaveData> m_playerDataByUuid;
    QSet<QString> m_dirtyPlayerUuids;

    // Dirty block chunk tracking for periodic flush
    QSet<DimChunkPos> m_dirtyBlockChunks;
    ChunkProvider m_chunkProvider;
};
