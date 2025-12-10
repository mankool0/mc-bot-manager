#include "BlockRegistry.h"
#include "logging/LogManager.h"
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QMutexLocker>

QString BlockRegistry::getCachePath(int dataVersion)
{
    return QString("cache/block_registry_%1.dat").arg(dataVersion);
}

void BlockRegistry::ensureCacheDirectory()
{
    QDir dir;
    if (!dir.exists("cache")) {
        dir.mkpath("cache");
    }
}

bool BlockRegistry::cacheExists(int dataVersion)
{
    return QFile::exists(getCachePath(dataVersion));
}

bool BlockRegistry::loadFromCache(int dataVersion)
{
    QMutexLocker locker(&mutex);

    QString cachePath = getCachePath(dataVersion);
    QFile file(cachePath);

    if (!file.open(QIODevice::ReadOnly)) {
        // This is normal on first run, so just Info level
        LogManager::log(QString("Block registry cache not found for data version %1").arg(dataVersion), LogManager::Info);
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
        LogManager::log(QString("Invalid block registry cache file: %1").arg(cachePath), LogManager::Warning);
        file.close();
        this->dataVersion = dataVersion;
        return false;
    }

    if (version != FORMAT_VERSION) {
        LogManager::log(QString("Unsupported block registry format version: %1 (expected %2)")
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
    idToState.clear();
    for (quint32 i = 0; i < size; ++i) {
        quint32 id;
        QString state;
        in >> id >> state;
        idToState[id] = state;
    }

    this->dataVersion = dataVersion;
    file.close();

    LogManager::log(QString("Loaded block registry from cache for data version %1 with %2 block states")
                   .arg(dataVersion).arg(size), LogManager::Success);
    return true;
}

void BlockRegistry::saveToCache()
{
    QMutexLocker locker(&mutex);

    if (dataVersion <= 0) {
        LogManager::log("Cannot save block registry: invalid data version", LogManager::Warning);
        return;
    }

    ensureCacheDirectory();

    QString cachePath = getCachePath(dataVersion);
    QFile file(cachePath);

    if (!file.open(QIODevice::WriteOnly)) {
        LogManager::log(QString("Failed to open block registry cache for writing: %1").arg(cachePath), LogManager::Error);
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);

    // Write header
    out << MAGIC_NUMBER;
    out << FORMAT_VERSION;

    // Write data version
    out << qint32(dataVersion);

    // Write registry size
    out << quint32(idToState.size());

    // Write all mappings
    for (auto it = idToState.constBegin(); it != idToState.constEnd(); ++it) {
        out << quint32(it.key()) << it.value();
    }

    file.close();

    LogManager::log(QString("Saved block registry to cache: %1 with %2 block states")
                   .arg(cachePath).arg(idToState.size()), LogManager::Success);
}

void BlockRegistry::addBlockState(uint32_t id, const QString& blockState)
{
    QMutexLocker locker(&mutex);
    idToState[id] = blockState;
}

std::optional<QString> BlockRegistry::getBlockState(uint32_t id) const
{
    QMutexLocker locker(&mutex);
    auto it = idToState.find(id);
    if (it != idToState.end()) {
        return *it;
    }
    return std::nullopt;
}
