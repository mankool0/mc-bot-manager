#include "ChunkSavingWorker.h"
#include "world/WorldExporter.h"
#include "logging/LogManager.h"
#include <QDir>

ChunkSavingWorker::ChunkSavingWorker(QObject *parent) : QObject(parent) {}
ChunkSavingWorker::~ChunkSavingWorker() = default;

void ChunkSavingWorker::processChunk(const ChunkData& chunk, const QString& worldPath, int dataVersion) {
    // Determine dimension path
    QString dimensionPath;
    QString dimensionName;
    if (chunk.dimension == "minecraft:the_nether") {
        dimensionPath = worldPath + "/DIM-1";
        dimensionName = "Nether";
    } else if (chunk.dimension == "minecraft:overworld") {
        dimensionPath = worldPath;
        dimensionName = "Overworld";
    } else if (chunk.dimension == "minecraft:the_end") {
        dimensionPath = worldPath + "/DIM1";
        dimensionName = "End";
    } else {
        LogManager::log(QString("Cannot save chunk with unknown dimension: %1").arg(chunk.dimension), LogManager::Warning);
        return;
    }

    // Ensure dimension directory exists
    QDir dir;
    if (!dir.exists(dimensionPath + "/region")) {
        LogManager::log(QString("Creating region directory for dimension %1").arg(dimensionName), LogManager::Info);
        dir.mkpath(dimensionPath + "/region");
    }

    // Save chunk (silent - this happens frequently)
    WorldExporter::exportChunk(chunk, dimensionPath, dataVersion);
}
