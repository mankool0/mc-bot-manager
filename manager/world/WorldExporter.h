#ifndef WORLDEXPORTER_H
#define WORLDEXPORTER_H

#include "bot/WorldData.h"
#include "world/RegionFile.h"
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QHash>
#include <memory>
#include <unordered_map>

struct MinecraftVersion {
    int dataVersion = 0;
    QString versionName;
    QString series = "main";
    bool isSnapshot = false;
};

struct RegionPos {
    int32_t x = 0;
    int32_t z = 0;

    RegionPos() = default;
    RegionPos(int32_t x, int32_t z) : x(x), z(z) {}

    bool operator==(const RegionPos& other) const {
        return x == other.x && z == other.z;
    }

    static RegionPos fromChunkPos(int chunkX, int chunkZ) {
        return RegionPos(chunkX >> 5, chunkZ >> 5);
    }
};

// Hash function for RegionPos to use in QHash
inline uint qHash(const RegionPos& pos, uint seed = 0) {
    return qHash(static_cast<qint64>(pos.x) << 32 | static_cast<quint32>(pos.z), seed);
}

// Hash function for RegionPos to use in std::unordered_map
namespace std {
    template<>
    struct hash<RegionPos> {
        size_t operator()(const RegionPos& pos) const noexcept {
            return std::hash<int64_t>{}(static_cast<int64_t>(pos.x) << 32 | static_cast<uint32_t>(pos.z));
        }
    };
}

// Exports world data to Minecraft save format with void generator.
class WorldExporter {
public:
    static bool exportWorld(const BotWorldData& worldData,
                           const QString& outputPath,
                           int spawnX, int spawnY, int spawnZ,
                           const QString& worldName,
                           const MinecraftVersion& version);

    static bool exportChunk(const ChunkData& chunk, const QString& outputPath, int dataVersion);
    static std::tuple<int, int, int, int> getChunkBounds(const BotWorldData& worldData);  // Returns (minX, maxX, minZ, maxZ)
    static bool createWorldDirectories(const QString& outputPath);

    static bool createLevelDat(const QString& outputPath,
                              int spawnX, int spawnY, int spawnZ,
                              const QString& worldName,
                              const MinecraftVersion& version);

private:
    static bool createSessionLock(const QString& outputPath);

    // Cached in memory during export
    static RegionFile* getRegionFile(const QString& outputPath,
                                    int regionX, int regionZ,
                                    std::unordered_map<RegionPos, std::unique_ptr<RegionFile>>& regionCache);

    static QString getRegionFilePath(const QString& outputPath, int regionX, int regionZ);
    static nbt::tag_compound createVoidDimension(const std::string& dimensionType);
};

#endif // WORLDEXPORTER_H
