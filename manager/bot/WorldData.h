#ifndef WORLDDATA_H
#define WORLDDATA_H

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector3D>
#include <QVector>
#include <optional>
#include <qobject.h>

struct ChunkPos {
    int32_t x = 0;
    int32_t z = 0;

    ChunkPos() = default;
    ChunkPos(int32_t x, int32_t z) : x(x), z(z) {}

    bool operator==(const ChunkPos& other) const {
        return x == other.x && z == other.z;
    }

    bool operator!=(const ChunkPos& other) const {
        return !(*this == other);
    }
};

// Hash function for ChunkPos to use in QHash
inline uint qHash(const ChunkPos& pos, uint seed = 0) {
    return qHash(static_cast<qint64>(pos.x) << 32 | static_cast<quint32>(pos.z), seed);
}

// 16x16x16 chunk section with palette-based block storage (matches Minecraft format).
struct ChunkSection {
    int32_t sectionY = 0;
    QVector<QString> palette;          // Block state strings (e.g., "minecraft:stone")
    QVector<uint32_t> blockIndices;    // 4096 entries in YZX order
    bool uniform = false;              // If true, entire section is palette[0]

    QString getBlock(int localX, int localY, int localZ) const;  // localX/Y/Z: 0-15; index order: y*256 + z*16 + x
    void setBlock(int localX, int localY, int localZ, const QString& blockState);
    size_t memoryUsage() const;
};

// Full chunk (16x16 columns, multiple sections vertically).
struct ChunkData {
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
    QString dimension;
    int32_t minY = -64;
    int32_t maxY = 320;
    QMap<int32_t, ChunkSection> sections;

    std::optional<QString> getBlock(int localX, int localY, int localZ) const;  // localX/Z: 0-15, localY: minY-maxY
    void setBlock(int localX, int localY, int localZ, const QString& blockState);
    size_t memoryUsage() const;
    int sectionCount() const { return sections.size(); }
};

Q_DECLARE_METATYPE(ChunkData);

// Stores world data for a bot
class BotWorldData {
public:
    BotWorldData() = default;

    std::optional<QString> getBlock(int x, int y, int z) const;  // Returns nullopt if chunk not loaded
    void setBlock(int x, int y, int z, const QString& blockState);  // Creates chunk/section if needed

    void loadChunk(const ChunkData& chunk);
    void unloadChunk(int chunkX, int chunkZ);
    bool isChunkLoaded(int chunkX, int chunkZ) const;
    const ChunkData* getChunk(int chunkX, int chunkZ) const;  // Returns nullptr if not loaded
    QVector<ChunkPos> getLoadedChunks() const;

    // Only searches loaded chunks
    QVector<QVector3D> findBlocks(const QString& blockType, const QVector3D& center, int radius) const;
    std::optional<QVector3D> findNearestBlock(const QStringList& blockTypes, const QVector3D& start, int maxDistance = 128) const;

    size_t totalMemoryUsage() const;
    int chunkCount() const { return chunks.size(); }
    QString getCurrentDimension() const { return currentDimension; }
    void setCurrentDimension(const QString& dimension) { currentDimension = dimension; }

private:
    QHash<ChunkPos, ChunkData> chunks;
    QString currentDimension;

    bool blockMatches(const QString& blockState, const QStringList& blockTypes) const;  // Handles exact matches and wildcards
};

#endif // WORLDDATA_H
