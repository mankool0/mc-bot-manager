#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QSet>
#include <QHash>
#include "bot/WorldData.h"
#include "world/WorldExporter.h"

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

    void saveChunkAsync(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities = {});
    void onEntitiesUpdated(const QVector<EntityData>& upserted, const QVector<int>& removed,
                           const QString& dimension);
    void setPlayerData(const PlayerSaveData& data);
    void flushPlayerData();

    int getDataVersion() const { return m_version.dataVersion; }

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
    QSet<QString> m_dirtyEntityChunks;  // "dimension|cx,cz"
    QTimer* m_periodicFlushTimer;

    // Player data
    PlayerSaveData m_latestPlayerData;
    bool m_playerDataDirty = false;
};
