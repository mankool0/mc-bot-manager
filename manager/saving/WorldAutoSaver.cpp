#include "WorldAutoSaver.h"
#include "bot/WorldData.h"
#include "world/WorldExporter.h"
#include "saving/ChunkSavingWorker.h"
#include "logging/LogManager.h"
#include <QDir>

WorldAutoSaver::WorldAutoSaver(const QString& serverIp, int dataVersion)
    : m_serverIp(serverIp), m_dataVersion(dataVersion), m_isInitialized(false) {

    // Sanitize server IP to be a valid directory name
    QString sanitizedIp = serverIp;
    sanitizedIp.replace(":", "_"); // e.g., 127.0.0.1:25565 -> 127.0.0.1_25565

    m_worldPath = "worldSaves/" + sanitizedIp;
    LogManager::log(QString("WorldAutoSaver initialized for %1 with data version %2. Saving to: %3")
                   .arg(serverIp).arg(dataVersion).arg(m_worldPath), LogManager::Info);

    // Setup worker thread
    m_workerThread = new QThread();
    m_worker = new ChunkSavingWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &WorldAutoSaver::chunkReadyForSaving, m_worker, &ChunkSavingWorker::processChunk, Qt::QueuedConnection);

    m_workerThread->start();

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
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_workerThread;
}

void WorldAutoSaver::saveChunkAsync(const ChunkData& chunk) {
    if (!m_isInitialized) {
        LogManager::log(QString("WorldAutoSaver not initialized, cannot save chunk (%1, %2)")
                       .arg(chunk.chunkX).arg(chunk.chunkZ), LogManager::Warning);
        return;
    }

    emit chunkReadyForSaving(chunk, m_worldPath, m_dataVersion);
}

void WorldAutoSaver::initializeWorld() {
    if (WorldExporter::createWorldDirectories(m_worldPath)) {
        MinecraftVersion version;
        version.dataVersion = m_dataVersion;
        version.versionName = "Unknown";
        version.series = "main";
        version.isSnapshot = false;

        if (WorldExporter::createLevelDat(m_worldPath, 0, 80, 0, m_serverIp, version)) {
            m_isInitialized = true;
            LogManager::log(QString("Successfully created new world structure for %1 with data version %2")
                           .arg(m_serverIp).arg(m_dataVersion), LogManager::Success);
        } else {
            LogManager::log(QString("Failed to create level.dat for %1").arg(m_serverIp), LogManager::Error);
        }
    }
    else {
        LogManager::log(QString("Failed to create world directories for %1").arg(m_serverIp), LogManager::Error);
    }
}
