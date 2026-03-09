#pragma once

#include <QObject>
#include "bot/WorldData.h"

class ChunkSavingWorker : public QObject {
    Q_OBJECT

public:
    explicit ChunkSavingWorker(QObject *parent = nullptr);
    ~ChunkSavingWorker();

public slots:
    void processChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities,
                      const QString& worldPath, int dataVersion);
    void processEntityChunk(int chunkX, int chunkZ, const QString& dimension,
                            const QVector<EntityData>& entities,
                            const QString& worldPath, int dataVersion);
    void processPlayerData(const PlayerSaveData& data, const QString& worldPath, int dataVersion);
};
