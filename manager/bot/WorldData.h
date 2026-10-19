#ifndef WORLDDATA_H
#define WORLDDATA_H

#include <QHash>
#include <QPoint>
#include <QSet>
#include <deque>
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
    // A bot opened it, so `items` is its whole contents and an empty list means empty. Without
    // it, no items means not known: the server never sends a container's contents in chunk data.
    bool contentsSeen = false;
};

struct MapData {
    int32_t scale = 0;
    bool locked = false;
    QString dimension;
    QByteArray colors; // 16384 bytes (128x128), accumulated from patches
    // Where it is centred, once a bot holding it has seen its own marker on it; the server
    // never sends it. With it, `dimension` is the map's own rather than the one it was seen in.
    std::optional<QPoint> center;
    quint64 seq = 0;   // BotWorldData's map sequence at its last change
};

// One marker on a map, as the map packet carries it.
struct MapDecorationData {
    QString type;      // "minecraft:player", ...
    int x = 0;         // half-pixels from the centre, -128..127
    int z = 0;
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
    int ownerEntityId = 0;
    QString ownerUuid;
    bool shortLived = false;
    QString customName;
    QByteArray rawNbt;    // full NBT as the client saved it (no "id"); empty until it sends one
    quint64 nbtSeq = 0;   // BotWorldData's entity sequence when rawNbt last changed
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
Q_DECLARE_METATYPE(MapData);
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

    // Entity tracking. `died` are also removed; they are remembered for entityNbtSince.
    void updateEntities(const QVector<EntityData>& upserted, const QVector<int>& removed,
                        const QString& dimension, const QVector<int>& died = {});

    // For world.entity_nbt(): entities whose NBT changed after `since`, and the uuids of those
    // seen dying after it. `token` is the sequence to pass next time.
    struct EntityNbtChanges {
        quint64 token = 0;
        QVector<EntityData> entities;
        QStringList died;
        QString dimension;
    };
    EntityNbtChanges entityNbtSince(quint64 since) const;

    // Map data from the client: a colour patch, and the markers when the packet carried them.
    // `holder` is the bot's position, used to place the map when its own marker is on it.
    void updateMap(int mapId, int scale, bool locked, const QString& dimension, int patchX, int patchZ,
                   int patchW, int patchH, const QByteArray& patch, const QVector<MapDecorationData>* decorations,
                   std::optional<QVector3D> holder);
    struct MapChanges {
        quint64 token = 0;
        QVector<QPair<int, MapData>> maps;
    };
    MapChanges mapsSince(quint64 since) const;
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
    QVector<BlockEntityData> getBlockEntitiesInDimension(const QString& dimension) const;
    // One pass over every block entity for a whole batch of columns, where getBlockEntitiesInChunk
    // would scan them all once per column.
    QHash<ChunkPos, QVector<BlockEntityData>> getBlockEntitiesInColumns(const QSet<ChunkPos>& columns,
                                                                        const QString& dimension) const;

private:
    QHash<ChunkPos, ChunkData> chunks;
    QString currentDimension;
    // Keyed by uuid, not entity id: the server hands out a fresh network id every time
    // an entity's chunk reloads, while the uuid lasts as long as the world does.
    QHash<QString, EntityData> entities;
    QHash<int, QString> entityIdToUuid;
    QString entityDimension;
    QHash<BlockEntityPos, BlockEntityData> blockEntities;
    // Sequences survive clearWorldState, so a caller's token stays valid across a reconnect.
    quint64 entityNbtSeq = 0;
    std::deque<std::pair<quint64, QString>> diedLog;  // (seq, uuid), oldest first, capped
    QHash<int, MapData> maps;
    quint64 mapSeq = 0;

    bool blockMatches(const QString& blockState, const QStringList& blockTypes) const;  // Handles exact matches and wildcards
};

#endif // WORLDDATA_H
