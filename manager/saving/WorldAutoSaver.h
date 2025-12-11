#pragma once

#include <QObject>
#include <QThread>
#include "bot/WorldData.h"
#include "world/WorldExporter.h"

class ChunkSavingWorker;

class WorldAutoSaver : public QObject {
    Q_OBJECT

public:
    explicit WorldAutoSaver(const QString& serverIp, const MinecraftVersion& version);
    ~WorldAutoSaver();

    void saveChunkAsync(const ChunkData& chunk);
    int getDataVersion() const { return m_version.dataVersion; }

signals:
    void chunkReadyForSaving(const ChunkData& chunk, const QString& worldPath, int dataVersion);

private:
    void initializeWorld();

    QString m_worldPath;
    QString m_serverIp;
    MinecraftVersion m_version;
    bool m_isInitialized;

    QThread* m_workerThread;
    ChunkSavingWorker* m_worker;
};
