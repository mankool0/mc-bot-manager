#ifndef WORLDDATA_H
#define WORLDDATA_H

#include <QHash>
#include <QMap>
#include <QByteArray>
#include <QString>
#include <QVector3D>
#include <QVector>
#include <optional>
#include <qobject.h>
#include "common.qpb.h"

struct BlockEntityData {
    int x = 0, y = 0, z = 0;
    QString dimension;
    QString type;                                        // e.g. "minecraft:chest"
    QVector<mankool::mcbot::protocol::ItemStack> items;
    QByteArray rawNbt;  // Full binary compound payload from chunk load; patched with items if container was opened
};

struct PlayerSaveData {
    QString uuid;
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    QString dimension;
    float health = 0.0f;
    int foodLevel = 0;
    float saturation = 0.0f;
    int experienceLevel = 0;
    float experienceProgress = 0.0f;
    int totalExperience = 0;
    QVector<mankool::mcbot::protocol::ItemStack> inventory;   // slots 0-40
    QVector<mankool::mcbot::protocol::ItemStack> enderItems;
};

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

struct EntityData {
    int entityId = 0;
    QString uuid;
    QString type;
    QString playerName;
    double x = 0, y = 0, z = 0;
    double velX = 0, velY = 0, velZ = 0;
    float yaw = 0, pitch = 0;
    float health = 0, maxHealth = 0;
    bool isLiving = false, isItem = false, isPlayer = false;
    mankool::mcbot::protocol::ItemStack itemStack;  // only populated when isItem == true
};

// Hash function for ChunkPos to use in QHash
inline uint qHash(const ChunkPos& pos, uint seed = 0) {
    return qHash(static_cast<qint64>(pos.x) << 32 | static_cast<quint32>(pos.z), seed);
}

struct BlockEntityPos {
    QString dimension;
    int x = 0, y = 0, z = 0;

    bool operator==(const BlockEntityPos& other) const {
        return x == other.x && y == other.y && z == other.z && dimension == other.dimension;
    }
};

inline size_t qHash(const BlockEntityPos& pos, size_t seed = 0) {
    return qHashMulti(seed, pos.dimension, pos.x, pos.y, pos.z);
}

// 16x16x16 chunk section with palette-based block storage (matches Minecraft format).
struct ChunkSection {
    int32_t sectionY = 0;
    QVector<QString> palette;          // Block state strings (e.g., "minecraft:stone")
    QVector<uint32_t> blockIndices;    // 4096 entries in YZX order
    bool uniform = false;              // If true, entire section is palette[0]
    QVector<QString> biomePalette;     // Biome resource IDs (e.g., "minecraft:plains")
    QVector<uint32_t> biomeIndices;    // 64 entries in 4x4x4 grid (index = y*16 + z*4 + x); empty if biomeUniform
    bool biomeUniform = false;         // If true, entire section is biomePalette[0]
    QByteArray blockLight;             // 2048-byte nibble array; empty if not present
    QByteArray skyLight;               // 2048-byte nibble array; empty in nether/end or if not present

    QString getBlock(int localX, int localY, int localZ) const;  // localX/Y/Z: 0-15; index order: y*256 + z*16 + x
    void setBlock(int localX, int localY, int localZ, const QString& blockState);

    struct LightLevels { int block = 0; int sky = 0; };
    LightLevels getLight(int localX, int localY, int localZ) const;  // localX/Y/Z: 0-15; returns 0 for absent light

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
    ChunkSection::LightLevels getLight(int localX, int localY, int localZ) const;  // returns {0,0} if section missing
    void setBlock(int localX, int localY, int localZ, const QString& blockState);
    size_t memoryUsage() const;
    int sectionCount() const { return sections.size(); }
};

Q_DECLARE_METATYPE(ChunkData);
Q_DECLARE_METATYPE(BlockEntityData);
Q_DECLARE_METATYPE(PlayerSaveData);
Q_DECLARE_METATYPE(QVector<EntityData>);
Q_DECLARE_METATYPE(QVector<BlockEntityData>);

// Stores world data for a bot
class BotWorldData {
public:
    BotWorldData() = default;

    std::optional<QString> getBlock(int x, int y, int z) const;  // Returns nullopt if chunk not loaded
    void setBlock(int x, int y, int z, const QString& blockState);  // Creates chunk/section if needed

    // Returns nullopt if chunk not loaded; block/sky are 0-15 (sky is 0 in nether/end)
    std::optional<ChunkSection::LightLevels> getLight(int x, int y, int z) const;
    // Apply incremental light updates for one section (empty data = clear to zero)
    void updateSectionBlockLight(int chunkX, int chunkZ, int sectionY, const QByteArray& data);
    void updateSectionSkyLight(int chunkX, int chunkZ, int sectionY, const QByteArray& data);

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

    // Entity tracking
    void updateEntities(const QVector<EntityData>& upserted, const QVector<int>& removed);
    QVector<EntityData> getAllEntities() const;
    QVector<EntityData> findEntitiesNear(double x, double y, double z, double radius,
                                         const QString& typeFilter = "") const;
    void clearEntities();
    void clearWorldState();

    // Block entity tracking
    void updateBlockEntity(const BlockEntityData& be);
    void removeBlockEntity(int x, int y, int z, const QString& dimension);
    std::optional<BlockEntityData> getBlockEntity(int x, int y, int z, const QString& dimension) const;
    QVector<BlockEntityData> getBlockEntitiesInChunk(int chunkX, int chunkZ, const QString& dimension) const;

private:
    QHash<ChunkPos, ChunkData> chunks;
    QString currentDimension;
    QHash<int, EntityData> entities;
    QHash<BlockEntityPos, BlockEntityData> blockEntities;

    bool blockMatches(const QString& blockState, const QStringList& blockTypes) const;  // Handles exact matches and wildcards
};

#endif // WORLDDATA_H
