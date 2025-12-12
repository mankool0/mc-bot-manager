#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QMap>
#include <QString>
#include <QMutex>
#include <optional>

// Block state registry for Minecraft data versions. Caches to ./cache/ directory.
class BlockRegistry {
public:
    static constexpr quint32 MAGIC_NUMBER = 0x424C4B52;  // "BLKR"
    static constexpr qint32 FORMAT_VERSION = 1;

    BlockRegistry() = default;

    bool loadFromCache(int dataVersion);  // Returns true if loaded from cache
    void saveToCache();
    void addBlockState(uint32_t id, const QString& blockState);
    std::optional<QString> getBlockState(uint32_t id) const;  // Returns nullopt if not found

    int getDataVersion() const { return dataVersion; }
    int size() const { return idToState.size(); }
    bool isLoaded() const { return dataVersion > 0 && !idToState.isEmpty(); }

    static bool cacheExists(int dataVersion);
    static QString getCachePath(int dataVersion);

private:
    int dataVersion = 0;
    QMap<uint32_t, QString> idToState;
    mutable QMutex mutex;

    static void ensureCacheDirectory();
};

#endif // BLOCKREGISTRY_H
