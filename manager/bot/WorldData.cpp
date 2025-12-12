#include "WorldData.h"
#include "logging/LogManager.h"
#include <QtMath>
#include <QRegularExpression>
#include <algorithm>

// ============================================================================
// ChunkSection Implementation
// ============================================================================

QString ChunkSection::getBlock(int localX, int localY, int localZ) const
{
    // Validate coordinates
    if (localX < 0 || localX >= 16 || localY < 0 || localY >= 16 || localZ < 0 || localZ >= 16) {
        LogManager::log(QString("ChunkSection: Invalid local coordinates: (%1, %2, %3)")
                       .arg(localX).arg(localY).arg(localZ), LogManager::Warning);
        return "minecraft:air";
    }

    // If uniform, entire section is the same block
    if (uniform) {
        return palette.isEmpty() ? "minecraft:air" : palette[0];
    }

    // Calculate index using YZX order: y*256 + z*16 + x
    int index = localY * 256 + localZ * 16 + localX;

    // Validate index
    if (index < 0 || index >= blockIndices.size()) {
        LogManager::log(QString("ChunkSection: Block index out of range: %1 (size: %2)")
                       .arg(index).arg(blockIndices.size()), LogManager::Warning);
        return "minecraft:air";
    }

    // Get palette index
    uint32_t paletteIndex = blockIndices[index];
    if (paletteIndex >= static_cast<uint32_t>(palette.size())) {
        LogManager::log(QString("ChunkSection: Palette index out of range: %1 (palette size: %2)")
                       .arg(paletteIndex).arg(palette.size()), LogManager::Warning);
        return "minecraft:air";
    }

    return palette[paletteIndex];
}

void ChunkSection::setBlock(int localX, int localY, int localZ, const QString& blockState)
{
    // Validate coordinates
    if (localX < 0 || localX >= 16 || localY < 0 || localY >= 16 || localZ < 0 || localZ >= 16) {
        LogManager::log(QString("ChunkSection::setBlock: Invalid local coordinates: (%1, %2, %3)")
                       .arg(localX).arg(localY).arg(localZ), LogManager::Warning);
        return;
    }

    // If currently uniform, we may need to expand
    if (uniform && !palette.isEmpty() && palette[0] != blockState) {
        // Expand uniform section to full palette
        uniform = false;
        blockIndices.resize(4096);
        blockIndices.fill(0); // All blocks point to palette[0]
    }

    // Find or add block state to palette
    int paletteIndex = palette.indexOf(blockState);
    if (paletteIndex == -1) {
        paletteIndex = palette.size();
        palette.append(blockState);
    }

    // If not uniform, update the block index
    if (!uniform) {
        int index = localY * 256 + localZ * 16 + localX;
        if (index >= blockIndices.size()) {
            blockIndices.resize(4096);
        }
        blockIndices[index] = paletteIndex;
    }
}

size_t ChunkSection::memoryUsage() const
{
    size_t total = sizeof(ChunkSection);

    // Palette strings
    for (const QString& str : palette) {
        total += str.size() * sizeof(QChar);
    }

    // Block indices
    total += blockIndices.size() * sizeof(uint32_t);

    return total;
}

// ============================================================================
// ChunkData Implementation
// ============================================================================

std::optional<QString> ChunkData::getBlock(int localX, int localY, int localZ) const
{
    // Validate coordinates
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16 || localY < minY || localY >= maxY) {
        return std::nullopt;
    }

    int sectionY = localY >> 4;  // Absolute section Y (e.g., -56 >> 4 = -4)

    // Check if section exists
    auto it = sections.find(sectionY);
    if (it == sections.end()) {
        return "minecraft:air";  // Non-existent sections are air
    }

    // Get local Y within section (0-15)
    int localSectionY = localY & 15;

    return it.value().getBlock(localX, localSectionY, localZ);
}

void ChunkData::setBlock(int localX, int localY, int localZ, const QString& blockState)
{
    // Validate coordinates
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        return;
    }
    if (localY < minY || localY >= maxY) {
        return;
    }

    int sectionY = localY >> 4;

    // Get local Y within section (0-15)
    int localSectionY = localY & 15;

    // Create section if it doesn't exist
    if (!sections.contains(sectionY)) {
        ChunkSection newSection;
        newSection.sectionY = sectionY;
        newSection.uniform = true;
        newSection.palette.append("minecraft:air");
        sections[sectionY] = newSection;
    }

    // Set block in section
    sections[sectionY].setBlock(localX, localSectionY, localZ, blockState);
}

size_t ChunkData::memoryUsage() const
{
    size_t total = sizeof(ChunkData);
    total += dimension.size() * sizeof(QChar);

    for (const ChunkSection& section : sections) {
        total += section.memoryUsage();
    }

    return total;
}

// ============================================================================
// BotWorldData Implementation
// ============================================================================

std::optional<QString> BotWorldData::getBlock(int x, int y, int z) const
{
    // Calculate chunk position
    ChunkPos chunkPos(x >> 4, z >> 4);

    // Check if chunk is loaded
    auto it = chunks.find(chunkPos);
    if (it == chunks.end()) {
        return std::nullopt;  // Chunk not loaded
    }

    // Get local coordinates within chunk
    int localX = x & 15;  // Modulo 16
    int localZ = z & 15;

    return it.value().getBlock(localX, y, localZ);
}

void BotWorldData::setBlock(int x, int y, int z, const QString& blockState)
{
    // Calculate chunk position
    ChunkPos chunkPos(x >> 4, z >> 4);

    // Get or create chunk
    if (!chunks.contains(chunkPos)) {
        ChunkData newChunk;
        newChunk.chunkX = chunkPos.x;
        newChunk.chunkZ = chunkPos.z;
        newChunk.dimension = currentDimension;
        chunks[chunkPos] = newChunk;
    }

    // Get local coordinates
    int localX = x & 15;
    int localZ = z & 15;

    chunks[chunkPos].setBlock(localX, y, localZ, blockState);
}

void BotWorldData::loadChunk(const ChunkData& chunk)
{
    ChunkPos pos(chunk.chunkX, chunk.chunkZ);
    chunks[pos] = chunk;
}

void BotWorldData::unloadChunk(int chunkX, int chunkZ)
{
    ChunkPos pos(chunkX, chunkZ);
    chunks.remove(pos);
}

bool BotWorldData::isChunkLoaded(int chunkX, int chunkZ) const
{
    ChunkPos pos(chunkX, chunkZ);
    return chunks.contains(pos);
}

const ChunkData* BotWorldData::getChunk(int chunkX, int chunkZ) const
{
    ChunkPos pos(chunkX, chunkZ);
    auto it = chunks.find(pos);
    if (it == chunks.end()) {
        return nullptr;
    }
    return &it.value();
}

QVector<ChunkPos> BotWorldData::getLoadedChunks() const
{
    return chunks.keys().toVector();
}

QVector<QVector3D> BotWorldData::findBlocks(const QString& blockType, const QVector3D& center, int radius) const
{
    QVector<QVector3D> results;

    const double radiusSq = static_cast<double>(radius) * radius;

    // Calculate chunk bounds
    int minChunkX = static_cast<int>(qFloor((center.x() - radius) / 16.0));
    int maxChunkX = static_cast<int>(qFloor((center.x() + radius) / 16.0));
    int minChunkZ = static_cast<int>(qFloor((center.z() - radius) / 16.0));
    int maxChunkZ = static_cast<int>(qFloor((center.z() + radius) / 16.0));

    int minY = qMax(static_cast<int>(center.y() - radius), -64);
    int maxY = qMin(static_cast<int>(center.y() + radius), 320);

    // Search all chunks in range (only loaded ones!)
    for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
        for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            // Skip unloaded chunks immediately
            const ChunkData* chunk = getChunk(chunkX, chunkZ);
            if (!chunk) continue;

            int chunkMinX = chunkX * 16;
            int chunkMaxX = chunkMinX + 15;
            int chunkMinZ = chunkZ * 16;
            int chunkMaxZ = chunkMinZ + 15;

            double closestX = qBound(chunkMinX, static_cast<int>(center.x()), chunkMaxX);
            double closestZ = qBound(chunkMinZ, static_cast<int>(center.z()), chunkMaxZ);
            double closestY = qBound(minY, static_cast<int>(center.y()), maxY);

            double dx = closestX - center.x();
            double dy = closestY - center.y();
            double dz = closestZ - center.z();

            if (dx*dx + dy*dy + dz*dz > radiusSq) {
                continue;  // Entire chunk is outside radius
            }

            // Search all blocks in this chunk within radius
            for (int x = 0; x < 16; ++x) {
                int worldX = chunkX * 16 + x;
                double dx = worldX - center.x();
                double dxSq = dx * dx;

                for (int z = 0; z < 16; ++z) {
                    int worldZ = chunkZ * 16 + z;
                    double dz = worldZ - center.z();
                    double dzSq = dz * dz;
                    double horizDistSq = dxSq + dzSq;

                    // Early-exit if horizontal distance alone exceeds radius
                    if (horizDistSq > radiusSq) continue;

                    for (int y = minY; y <= maxY; ++y) {
                        double dy = y - center.y();
                        double distSq = horizDistSq + dy*dy;

                        if (distSq > radiusSq) continue;

                        // Check block type
                        auto block = chunk->getBlock(x, y, z);
                        if (block && blockMatches(*block, QStringList{blockType})) {
                            results.append(QVector3D(worldX, y, worldZ));
                        }
                    }
                }
            }
        }
    }

    return results;
}

std::optional<QVector3D> BotWorldData::findNearestBlock(const QStringList& blockTypes, const QVector3D& start, int maxDistance) const
{
    std::optional<QVector3D> nearest;
    double nearestDistSq = static_cast<double>(maxDistance) * maxDistance;

    // Calculate chunk bounds
    int minChunkX = static_cast<int>(qFloor((start.x() - maxDistance) / 16.0));
    int maxChunkX = static_cast<int>(qFloor((start.x() + maxDistance) / 16.0));
    int minChunkZ = static_cast<int>(qFloor((start.z() - maxDistance) / 16.0));
    int maxChunkZ = static_cast<int>(qFloor((start.z() + maxDistance) / 16.0));

    int minY = qMax(static_cast<int>(start.y() - maxDistance), -64);
    int maxY = qMin(static_cast<int>(start.y() + maxDistance), 320);

    // Search all chunks in range (spiral from center for early-exit potential)
    for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
        for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            // Skip unloaded chunks
            const ChunkData* chunk = getChunk(chunkX, chunkZ);
            if (!chunk) continue;

            int chunkMinX = chunkX * 16;
            int chunkMaxX = chunkMinX + 15;
            int chunkMinZ = chunkZ * 16;
            int chunkMaxZ = chunkMinZ + 15;

            double closestX = qBound(chunkMinX, static_cast<int>(start.x()), chunkMaxX);
            double closestZ = qBound(chunkMinZ, static_cast<int>(start.z()), chunkMaxZ);
            double closestY = qBound(minY, static_cast<int>(start.y()), maxY);

            double dx = closestX - start.x();
            double dy = closestY - start.y();
            double dz = closestZ - start.z();

            if (dx*dx + dy*dy + dz*dz >= nearestDistSq) {
                continue;  // Entire chunk is farther than current best
            }

            // Search all blocks in this chunk
            for (int x = 0; x < 16; ++x) {
                int worldX = chunkX * 16 + x;
                double dx = worldX - start.x();
                double dxSq = dx * dx;

                for (int z = 0; z < 16; ++z) {
                    int worldZ = chunkZ * 16 + z;
                    double dz = worldZ - start.z();
                    double dzSq = dz * dz;
                    double horizDistSq = dxSq + dzSq;

                    // Early-exit if horizontal distance alone exceeds current best
                    if (horizDistSq >= nearestDistSq) continue;

                    for (int y = minY; y <= maxY; ++y) {
                        double dy = y - start.y();
                        double distSq = horizDistSq + dy*dy;

                        if (distSq >= nearestDistSq) continue;

                        // Check block type
                        auto block = chunk->getBlock(x, y, z);
                        if (block && blockMatches(*block, blockTypes)) {
                            nearest = QVector3D(worldX, y, worldZ);
                            nearestDistSq = distSq;
                        }
                    }
                }
            }
        }
    }

    return nearest;
}

size_t BotWorldData::totalMemoryUsage() const
{
    size_t total = sizeof(BotWorldData);

    for (const ChunkData& chunk : chunks) {
        total += chunk.memoryUsage();
    }

    return total;
}

bool BotWorldData::blockMatches(const QString& blockState, const QStringList& blockTypes) const
{
    for (const QString& type : blockTypes) {
        // Exact match
        if (blockState == type) {
            return true;
        }

        // Match without properties (e.g., "minecraft:chest" matches "minecraft:chest[facing=north]")
        if (blockState.startsWith(type + "[")) {
            return true;
        }

        // Wildcard match (e.g., "*:chest" or "minecraft:*_ore")
        if (type.contains('*')) {
            QString pattern = QRegularExpression::escape(type);
            pattern.replace("\\*", ".*");
            QRegularExpression regex("^" + pattern);
            if (regex.match(blockState).hasMatch()) {
                return true;
            }
        }
    }

    return false;
}
