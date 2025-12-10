#pragma once

#include <QObject>
#include <QThread>
#include "bot/WorldData.h"

class ChunkSavingWorker;

class WorldAutoSaver : public QObject {
    Q_OBJECT

public:
    explicit WorldAutoSaver(const QString& serverIp, int dataVersion);
    ~WorldAutoSaver();

    void saveChunkAsync(const ChunkData& chunk);
    int getDataVersion() const { return m_dataVersion; }

signals:
    void chunkReadyForSaving(const ChunkData& chunk, const QString& worldPath, int dataVersion);

private:
    void initializeWorld();

    QString m_worldPath;
    QString m_serverIp;
    int m_dataVersion;
    bool m_isInitialized;

    QThread* m_workerThread;
    ChunkSavingWorker* m_worker;
};
