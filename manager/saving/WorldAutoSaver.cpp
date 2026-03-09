#include "WorldAutoSaver.h"
#include "bot/WorldData.h"
#include "world/WorldExporter.h"
#include "saving/ChunkSavingWorker.h"
#include "logging/LogManager.h"
#include "ui/ManagerMainWindow.h"
#include <QDir>
#include <cmath>

WorldAutoSaver::WorldAutoSaver(const QString& serverIp, const MinecraftVersion& version,
                               const WorldSaveSettings& settings)
    : m_serverIp(serverIp), m_version(version), m_saveSettings(settings), m_isInitialized(false) {

    // Sanitize server IP to be a valid directory name
    QString sanitizedIp = serverIp;
    sanitizedIp.replace(":", "_"); // e.g., 127.0.0.1:25565 -> 127.0.0.1_25565

    QString basePath = ManagerMainWindow::getWorldSaveBasePath();
    m_worldPath = basePath + "/" + sanitizedIp;
    LogManager::log(QString("WorldAutoSaver initialized for %1 with version %2 (data version %3). Saving to: %4")
                   .arg(serverIp).arg(version.versionName).arg(version.dataVersion).arg(m_worldPath), LogManager::Info);

    // Setup worker thread
    m_workerThread = new QThread();
    m_worker = new ChunkSavingWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &WorldAutoSaver::chunkReadyForSaving, m_worker, &ChunkSavingWorker::processChunk, Qt::QueuedConnection);
    connect(this, &WorldAutoSaver::entityChunkReadyForSaving, m_worker, &ChunkSavingWorker::processEntityChunk, Qt::QueuedConnection);
    connect(this, &WorldAutoSaver::playerDataReadyForSaving, m_worker, &ChunkSavingWorker::processPlayerData, Qt::QueuedConnection);

    m_workerThread->start();

    // Setup periodic flush timer (30 seconds)
    m_periodicFlushTimer = new QTimer(this);
    m_periodicFlushTimer->setInterval(30000);
    connect(m_periodicFlushTimer, &QTimer::timeout, this, &WorldAutoSaver::flushPeriodic);
    m_periodicFlushTimer->start();

    // Create world directories if they don't exist
    QDir dir(m_worldPath);
    if (!dir.exists()) {
        LogManager::log(QString("World directory doesn't exist for %1, creating new world...").arg(serverIp), LogManager::Info);
        initializeWorld();
    } else {
        LogManager::log(QString("World directory exists for %1, appending to existing world.").arg(serverIp), LogManager::Info);
        m_isInitialized = true;
    }
}

WorldAutoSaver::~WorldAutoSaver() {
    // Flush player data on destruction
    if (m_isInitialized && m_saveSettings.savePlayerData && m_playerDataDirty) {
        emit playerDataReadyForSaving(m_latestPlayerData, m_worldPath, m_version.dataVersion);
    }

    m_workerThread->quit();
    m_workerThread->wait();
    delete m_workerThread;
}

void WorldAutoSaver::saveChunkAsync(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities) {
    if (!m_isInitialized) {
        LogManager::log(QString("WorldAutoSaver not initialized, cannot save chunk (%1, %2)")
                       .arg(chunk.chunkX).arg(chunk.chunkZ), LogManager::Warning);
        return;
    }

    QVector<BlockEntityData> filteredBEs;
    if (m_saveSettings.saveBlockEntities) {
        filteredBEs = blockEntities;
    }

    emit chunkReadyForSaving(chunk, filteredBEs, m_worldPath, m_version.dataVersion);
}

void WorldAutoSaver::onEntitiesUpdated(const QVector<EntityData>& upserted, const QVector<int>& removed,
                                       const QString& dimension) {
    if (!m_saveSettings.saveEntities) return;

    for (const auto& e : upserted) {
        if (e.isPlayer) continue;  // Players are saved in playerdata/
        if (!m_saveSettings.saveItemEntities && e.isItem) continue;

        m_trackedEntities[e.entityId] = {e, dimension};

        int chunkX = static_cast<int>(std::floor(e.x / 16.0));
        int chunkZ = static_cast<int>(std::floor(e.z / 16.0));
        m_dirtyEntityChunks.insert(QString("%1|%2,%3").arg(dimension).arg(chunkX).arg(chunkZ));
    }

    for (int id : removed) {
        auto it = m_trackedEntities.find(id);
        if (it != m_trackedEntities.end()) {
            int chunkX = static_cast<int>(std::floor(it->data.x / 16.0));
            int chunkZ = static_cast<int>(std::floor(it->data.z / 16.0));
            m_dirtyEntityChunks.insert(QString("%1|%2,%3").arg(it->dimension).arg(chunkX).arg(chunkZ));
            m_trackedEntities.erase(it);
        }
    }
}

void WorldAutoSaver::setPlayerData(const PlayerSaveData& data) {
    if (!m_saveSettings.savePlayerData) return;
    m_latestPlayerData = data;
    m_playerDataDirty = true;
}

void WorldAutoSaver::flushPlayerData() {
    if (!m_isInitialized || !m_saveSettings.savePlayerData || !m_playerDataDirty) return;
    m_playerDataDirty = false;
    emit playerDataReadyForSaving(m_latestPlayerData, m_worldPath, m_version.dataVersion);
}

void WorldAutoSaver::flushPeriodic() {
    if (!m_isInitialized) return;

    // Flush entities
    if (m_saveSettings.saveEntities && !m_dirtyEntityChunks.isEmpty()) {
        QSet<QString> dirtyChunks = m_dirtyEntityChunks;
        m_dirtyEntityChunks.clear();

        // Group entities by "dimension|cx,cz" key
        QHash<QString, QVector<EntityData>> chunkEntityMap;
        for (const auto& tracked : m_trackedEntities) {
            int chunkX = static_cast<int>(std::floor(tracked.data.x / 16.0));
            int chunkZ = static_cast<int>(std::floor(tracked.data.z / 16.0));
            QString key = QString("%1|%2,%3").arg(tracked.dimension).arg(chunkX).arg(chunkZ);
            chunkEntityMap[key].append(tracked.data);
        }

        for (const QString& key : dirtyChunks) {
            int sepIdx = key.indexOf('|');
            if (sepIdx < 0) continue;
            QString dim = key.left(sepIdx);
            QString coordStr = key.mid(sepIdx + 1);
            int commaIdx = coordStr.indexOf(',');
            if (commaIdx < 0) continue;
            int chunkX = coordStr.left(commaIdx).toInt();
            int chunkZ = coordStr.mid(commaIdx + 1).toInt();

            QVector<EntityData> entities = chunkEntityMap.value(key);
            emit entityChunkReadyForSaving(chunkX, chunkZ, dim, entities, m_worldPath, m_version.dataVersion);
        }
    }

    // Flush player data
    if (m_saveSettings.savePlayerData && m_playerDataDirty) {
        m_playerDataDirty = false;
        emit playerDataReadyForSaving(m_latestPlayerData, m_worldPath, m_version.dataVersion);
    }
}

void WorldAutoSaver::initializeWorld() {
    if (WorldExporter::createWorldDirectories(m_worldPath)) {
        if (WorldExporter::createLevelDat(m_worldPath, 0, 80, 0, m_serverIp, m_version)) {
            m_isInitialized = true;
            LogManager::log(QString("Successfully created new world structure for %1 with version %2 (data version %3)")
                           .arg(m_serverIp).arg(m_version.versionName).arg(m_version.dataVersion), LogManager::Success);
        } else {
            LogManager::log(QString("Failed to create level.dat for %1").arg(m_serverIp), LogManager::Error);
        }
    }
    else {
        LogManager::log(QString("Failed to create world directories for %1").arg(m_serverIp), LogManager::Error);
    }
}
