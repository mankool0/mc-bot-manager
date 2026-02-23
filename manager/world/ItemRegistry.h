#ifndef ITEMREGISTRY_H
#define ITEMREGISTRY_H

#include <QMap>
#include <QString>
#include <QMutex>
#include <optional>

struct ItemInfo {
    QString itemId;
    int maxStackSize;
    int maxDamage;
};

class ItemRegistry {
public:
    static constexpr quint32 MAGIC_NUMBER = 0x4954454D;  // "ITEM"
    static constexpr qint32 FORMAT_VERSION = 1;

    ItemRegistry() = default;

    bool loadFromCache(int dataVersion);
    void saveToCache();
    void addItem(const QString& itemId, int maxStackSize, int maxDamage);
    std::optional<ItemInfo> getItem(const QString& itemId) const;

    int getDataVersion() const { return dataVersion; }
    void setDataVersion(int version) { dataVersion = version; }
    int size() const { return items.size(); }
    bool isLoaded() const { return dataVersion > 0 && !items.isEmpty(); }

    static bool cacheExists(int dataVersion);
    static QString getCachePath(int dataVersion);

private:
    int dataVersion = 0;
    QMap<QString, ItemInfo> items;
    mutable QMutex mutex;

    static void ensureCacheDirectory();
};

#endif // ITEMREGISTRY_H
