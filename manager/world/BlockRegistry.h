#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QMap>
#include <QHash>
#include <QString>
#include <QMutex>
#include <optional>

// Block state registry for Minecraft data versions. Caches to ./cache/ directory.
class BlockRegistry {
public:
    enum class Direction {
        DOWN  = 0,
        UP    = 1,
        NORTH = 2,
        SOUTH = 3,
        WEST  = 4,
        EAST  = 5
    };
    static constexpr quint32 MAGIC_NUMBER = 0x424C4B52;  // "BLKR"
    static constexpr qint32 FORMAT_VERSION = 2;

    BlockRegistry() = default;

    bool loadFromCache(int dataVersion);  // Returns true if loaded from cache
    void saveToCache();
    void addBlockState(uint32_t id, const QString& blockState);
    void setFaceMask(uint32_t id, uint8_t mask);
    std::optional<QString> getBlockState(uint32_t id) const;
    std::optional<uint32_t> getStateId(const QString& blockState) const;
    bool isFaceSolid(uint32_t stateId, Direction direction) const;

    int getDataVersion() const { return dataVersion; }
    int size() const { return idToState.size(); }
    bool isLoaded() const { return dataVersion > 0 && !idToState.isEmpty(); }

    static bool cacheExists(int dataVersion);
    static QString getCachePath(int dataVersion);

private:
    int dataVersion = 0;
    QMap<uint32_t, QString> idToState;
    QHash<QString, uint32_t> stateToId;
    QHash<uint32_t, uint8_t> faceMasks;
    mutable QMutex mutex;

    static void ensureCacheDirectory();
};

#endif // BLOCKREGISTRY_H
