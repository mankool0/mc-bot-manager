#include "ItemRegistry.h"
#include "logging/LogManager.h"
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QMutexLocker>

QString ItemRegistry::getCachePath(int dataVersion)
{
    return QString("cache/item_registry_%1.dat").arg(dataVersion);
}

void ItemRegistry::ensureCacheDirectory()
{
    QDir dir;
    if (!dir.exists("cache")) {
        dir.mkpath("cache");
    }
}

bool ItemRegistry::cacheExists(int dataVersion)
{
    return QFile::exists(getCachePath(dataVersion));
}

bool ItemRegistry::loadFromCache(int dataVersion)
{
    QMutexLocker locker(&mutex);

    QString cachePath = getCachePath(dataVersion);
    QFile file(cachePath);

    if (!file.open(QIODevice::ReadOnly)) {
        LogManager::log(QString("Item registry cache not found for data version %1").arg(dataVersion), LogManager::Info);
        this->dataVersion = dataVersion;
        return false;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);

    // Read header
    quint32 magic;
    qint32 version;
    in >> magic >> version;

    if (magic != MAGIC_NUMBER) {
        LogManager::log(QString("Invalid item registry cache file: %1").arg(cachePath), LogManager::Warning);
        file.close();
        this->dataVersion = dataVersion;
        return false;
    }

    if (version != FORMAT_VERSION) {
        LogManager::log(QString("Unsupported item registry format version: %1 (expected %2)")
                       .arg(version).arg(FORMAT_VERSION), LogManager::Warning);
        file.close();
        this->dataVersion = dataVersion;
        return false;
    }

    // Read data version
    qint32 storedDataVersion;
    in >> storedDataVersion;

    if (storedDataVersion != dataVersion) {
        LogManager::log(QString("Data version mismatch in cache file. Expected: %1, Got: %2")
                       .arg(dataVersion).arg(storedDataVersion), LogManager::Warning);
        file.close();
        this->dataVersion = dataVersion;
        return false;
    }

    // Read registry size
    quint32 size;
    in >> size;

    // Read all mappings
    items.clear();
    for (quint32 i = 0; i < size; ++i) {
        QString id;
        qint32 stackSize;
        qint32 maxDmg;
        in >> id >> stackSize >> maxDmg;
        items[id] = ItemInfo{id, stackSize, maxDmg};
    }

    this->dataVersion = dataVersion;
    file.close();

    LogManager::log(QString("Loaded item registry from cache for data version %1 with %2 items")
                   .arg(dataVersion).arg(size), LogManager::Success);
    return true;
}

void ItemRegistry::saveToCache()
{
    QMutexLocker locker(&mutex);

    if (dataVersion <= 0) {
        LogManager::log("Cannot save item registry: invalid data version", LogManager::Warning);
        return;
    }

    ensureCacheDirectory();

    QString cachePath = getCachePath(dataVersion);
    QFile file(cachePath);

    if (!file.open(QIODevice::WriteOnly)) {
        LogManager::log(QString("Failed to open item registry cache for writing: %1").arg(cachePath), LogManager::Error);
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);

    // Write header
    out << MAGIC_NUMBER;
    out << FORMAT_VERSION;

    out << qint32(dataVersion);
    out << quint32(items.size());

    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        out << it.value().itemId << qint32(it.value().maxStackSize) << qint32(it.value().maxDamage);
    }

    file.close();

    LogManager::log(QString("Saved item registry to cache: %1 with %2 items")
                   .arg(cachePath).arg(items.size()), LogManager::Success);
}

void ItemRegistry::addItem(const QString& itemId, int maxStackSize, int maxDamage)
{
    QMutexLocker locker(&mutex);
    items[itemId] = ItemInfo{itemId, maxStackSize, maxDamage};
}

std::optional<ItemInfo> ItemRegistry::getItem(const QString& itemId) const
{
    QMutexLocker locker(&mutex);
    auto it = items.find(itemId);
    if (it != items.end()) {
        return *it;
    }
    return std::nullopt;
}
