#include "ChunkSavingWorker.h"
#include "world/WorldExporter.h"
#include "world/RegionFile.h"
#include "world/NBTSerializer.h"
#include "logging/LogManager.h"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QSet>
#include <algorithm>
#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <nbt_tags.h>
#include <fstream>
#include <sstream>
#include <zlib.h>

// Reads the EnderItems list from an existing gzip-compressed player.dat file.
// Returns nullopt if the file can't be read, isn't valid NBT, or has no EnderItems.
static std::optional<nbt::tag_list> readEnderItemsFromPlayerDat(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    QByteArray compressed = f.readAll();
    f.close();

    // Gzip decompress
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return std::nullopt;

    zs.next_in  = reinterpret_cast<Bytef*>(compressed.data());
    zs.avail_in = static_cast<uInt>(compressed.size());

    std::string decompressed;
    char buf[32768];
    int ret;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return std::nullopt; }
        decompressed.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    inflateEnd(&zs);

    // Parse NBT
    try {
        std::istringstream ss(decompressed, std::ios::binary);
        auto [name, rootPtr] = nbt::io::read_compound(ss);
        if (rootPtr && rootPtr->has_key("EnderItems", nbt::tag_type::List)) {
            return std::move(static_cast<nbt::tag_list&>(rootPtr->at("EnderItems").get()));
        }
    } catch (...) {}

    return std::nullopt;
}

// Only these ever get an Items list written by the manager, so only for these can the copy on
// disk hold contents the bot has not seen this session. Recovering them means inflating and
// parsing the whole stored chunk, which for a sign or a banner buys nothing.
static bool blockEntityHoldsItems(const QString& type) {
    static const QSet<QString> kTypes = {
        "minecraft:chest", "minecraft:trapped_chest", "minecraft:barrel", "minecraft:hopper",
        "minecraft:dispenser", "minecraft:dropper", "minecraft:furnace", "minecraft:blast_furnace",
        "minecraft:smoker", "minecraft:brewing_stand", "minecraft:crafter",
        "minecraft:chiseled_bookshelf", "minecraft:campfire", "minecraft:shulker_box",
    };
    return kTypes.contains(type) || type.endsWith(QLatin1String("_shulker_box"));
}

static constexpr int kMaxOpenRegions = 16;
static constexpr int kBacklogWarnChunks = 1000;
static constexpr qint64 kBacklogWarnIntervalMs = 60000;

ChunkSavingWorker::ChunkSavingWorker(const QString& worldPath, int dataVersion, QObject *parent)
    : QObject(parent), m_worldPath(worldPath), m_dataVersion(dataVersion) {}

ChunkSavingWorker::~ChunkSavingWorker() = default;

void ChunkSavingWorker::enqueueChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities) {
    DimChunkPos key{chunk.dimension, chunk.chunkX, chunk.chunkZ};
    bool schedule = false;
    int backlog = 0;
    {
        QMutexLocker lock(&m_pendingMutex);
        auto it = m_pending.find(key);
        if (it == m_pending.end()) {
            m_pending.insert(key, {chunk, blockEntities});
            m_pendingOrder.push_back(key);
        } else {
            *it = {chunk, blockEntities};
        }
        if (!m_drainScheduled) {
            m_drainScheduled = true;
            schedule = true;
        }
        backlog = m_pending.size();
    }

    if (backlog >= kBacklogWarnChunks
        && (!m_backlogWarning.isValid() || m_backlogWarning.elapsed() >= kBacklogWarnIntervalMs)) {
        m_backlogWarning.start();
        LogManager::log(QString("World saver for %1 has %2 chunks waiting; the writer is not keeping up")
                       .arg(m_worldPath).arg(backlog), LogManager::Warning);
    }

    if (schedule) {
        QMetaObject::invokeMethod(this, &ChunkSavingWorker::savePendingChunk, Qt::QueuedConnection);
    }
}

int ChunkSavingWorker::pendingChunkCount() const {
    QMutexLocker lock(&m_pendingMutex);
    return m_pending.size();
}

// One chunk per event so entity and player saves on this thread interleave with the drain
// instead of waiting behind a long backlog.
void ChunkSavingWorker::savePendingChunk() {
    PendingChunk next;
    {
        QMutexLocker lock(&m_pendingMutex);
        if (m_pendingOrder.empty()) {
            m_drainScheduled = false;
            return;
        }
        DimChunkPos key = std::move(m_pendingOrder.front());
        m_pendingOrder.pop_front();
        next = m_pending.take(key);
        m_drainScheduled = !m_pendingOrder.empty();
        if (m_drainScheduled) {
            QMetaObject::invokeMethod(this, &ChunkSavingWorker::savePendingChunk, Qt::QueuedConnection);
        }
    }
    processChunk(next.chunk, next.blockEntities);
}

RegionFile* ChunkSavingWorker::openRegion(const QString& path) {
    for (auto it = m_openRegions.begin(); it != m_openRegions.end(); ++it) {
        if (it->first != path) continue;
        // A save directory deleted underneath the manager would otherwise keep being written
        // through the unlinked handle.
        if (!QFile::exists(path)) {
            m_openRegions.erase(it);
            break;
        }
        std::rotate(it, it + 1, m_openRegions.end());
        return m_openRegions.back().second.get();
    }

    auto region = std::make_unique<RegionFile>(path);
    if (!region->isValid()) {
        return nullptr;
    }
    if (m_openRegions.size() >= static_cast<size_t>(kMaxOpenRegions)) {
        m_openRegions.erase(m_openRegions.begin());
    }
    m_openRegions.emplace_back(path, std::move(region));
    return m_openRegions.back().second.get();
}

void ChunkSavingWorker::processChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities) {
    const QString& worldPath = m_worldPath;
    const int dataVersion = m_dataVersion;
    // Determine dimension path (version-aware: 26.1+ uses dimensions/ subdirectory)
    QString dimensionPath = WorldExporter::getDimensionPath(worldPath, chunk.dimension, dataVersion);
    QString dimensionName;
    if (chunk.dimension == "minecraft:the_nether") {
        dimensionName = "Nether";
    } else if (chunk.dimension == "minecraft:overworld") {
        dimensionName = "Overworld";
    } else if (chunk.dimension == "minecraft:the_end") {
        dimensionName = "End";
    } else {
        LogManager::log(QString("Cannot save chunk with unknown dimension: %1").arg(chunk.dimension), LogManager::Warning);
        return;
    }

    // Ensure dimension directory exists
    QDir dir;
    if (!dir.exists(dimensionPath + "/region")) {
        LogManager::log(QString("Creating region directory for dimension %1").arg(dimensionName), LogManager::Info);
        dir.mkpath(dimensionPath + "/region");
    }

    // For block entities with rawNbt but no items (containers not opened this session),
    // recover their stored items from the existing .mca file so they aren't overwritten
    // with empty contents on reconnect.
    bool hasUnknownContainers = false;
    for (const auto& be : blockEntities) {
        if (!be.rawNbt.isEmpty() && be.items.isEmpty() && blockEntityHoldsItems(be.type)) {
            hasUnknownContainers = true;
            break;
        }
    }

    int regionX = chunk.chunkX >> 5;
    int regionZ = chunk.chunkZ >> 5;
    RegionFile* region = openRegion(QString("%1/region/r.%2.%3.mca").arg(dimensionPath).arg(regionX).arg(regionZ));
    if (!region) {
        LogManager::log(QString("Failed to open region r.%1.%2 in %3 for writing")
                       .arg(regionX).arg(regionZ).arg(dimensionPath), LogManager::Error);
        return;
    }

    const int localX = chunk.chunkX & 31;
    const int localZ = chunk.chunkZ & 31;

    QVector<BlockEntityData> effectiveBEs = blockEntities;
    if (hasUnknownContainers && region->hasChunk(localX, localZ)) {
        {
            {
                nbt::tag_compound existingChunk = region->readChunk(localX, localZ);

                if (existingChunk.has_key("block_entities", nbt::tag_type::List)) {
                    auto& beList = static_cast<nbt::tag_list&>(existingChunk.at("block_entities").get());

                    // Map "x,y,z" -> compound payload bytes for disk block entities that have items
                    QHash<QString, QByteArray> diskBEBytes;
                    for (size_t i = 0; i < beList.size(); ++i) {
                        try {
                            auto& beCompound = static_cast<nbt::tag_compound&>(beList[i].get());
                            if (!beCompound.has_key("Items", nbt::tag_type::List)) continue;
                            if (static_cast<nbt::tag_list&>(beCompound.at("Items").get()).size() == 0) continue;
                            if (!beCompound.has_key("x") || !beCompound.has_key("y") || !beCompound.has_key("z")) continue;

                            int bx = static_cast<nbt::tag_int&>(beCompound.at("x").get()).get();
                            int by = static_cast<nbt::tag_int&>(beCompound.at("y").get()).get();
                            int bz = static_cast<nbt::tag_int&>(beCompound.at("z").get()).get();

                            // write_tag writes: tag_type(1) + name_len(2) + name + payload.
                            // Skip the 3-byte header to get compound payload format matching rawNbt.
                            std::ostringstream out(std::ios::binary);
                            nbt::io::write_tag("", beCompound, out);
                            std::string bytes = out.str();
                            if (bytes.size() > 3) {
                                diskBEBytes[QString("%1,%2,%3").arg(bx).arg(by).arg(bz)] =
                                    QByteArray(bytes.data() + 3, static_cast<qsizetype>(bytes.size() - 3));
                            }
                        } catch (...) {}
                    }

                    // Inject disk items into block entities we don't know the contents of
                    for (auto& be : effectiveBEs) {
                        if (!be.rawNbt.isEmpty() && be.items.isEmpty()) {
                            auto it = diskBEBytes.find(QString("%1,%2,%3").arg(be.x).arg(be.y).arg(be.z));
                            if (it != diskBEBytes.end()) {
                                be.rawNbt = *it;  // Replace with disk version which has Items
                            }
                        }
                    }
                }
            }
        }
    }

    WorldExporter::exportChunk(chunk, *region, dataVersion, effectiveBEs);
    // Readers open their own handle, so what the kernel has is what a scan of the save sees.
    region->flush();
}

void ChunkSavingWorker::processEntityChunk(int chunkX, int chunkZ, const QString& dimension,
                                            const QVector<EntityData>& entities,
                                            const QString& worldPath, int dataVersion) {
    // Determine entities directory (version-aware: 26.1+ uses dimensions/ subdirectory)
    if (dimension != "minecraft:the_nether" && dimension != "minecraft:overworld" && dimension != "minecraft:the_end") {
        LogManager::log(QString("Cannot save entities with unknown dimension: %1").arg(dimension), LogManager::Warning);
        return;
    }
    QString entitiesDir = WorldExporter::getDimensionPath(worldPath, dimension, dataVersion) + "/entities";

    RegionFile* region = openRegion(QString("%1/r.%2.%3.mca").arg(entitiesDir).arg(chunkX >> 5).arg(chunkZ >> 5));
    if (!region) {
        LogManager::log(QString("Failed to open entity region for chunk (%1, %2) in %3")
                       .arg(chunkX).arg(chunkZ).arg(entitiesDir), LogManager::Error);
        return;
    }

    WorldExporter::exportEntityChunk(chunkX, chunkZ, entities, *region, dataVersion);
    region->flush();
}

void ChunkSavingWorker::processPlayerData(const PlayerSaveData& data, const QString& worldPath, int dataVersion) {
    if (data.uuid.isEmpty()) {
        LogManager::log("Cannot save player data: UUID is empty", LogManager::Warning);
        return;
    }

    // Build NBT
    nbt::tag_compound playerNBT = NBTSerializer::playerToNBT(data, dataVersion);

    // If ender chest wasn't opened this session, preserve EnderItems from the existing file
    // rather than overwriting with an empty list.
    QString playerdataDir = WorldExporter::getPlayerDataPath(worldPath, dataVersion);
    if (data.enderItems.isEmpty()) {
        QString filePath = playerdataDir + "/" + data.uuid + ".dat";
        auto existing = readEnderItemsFromPlayerDat(filePath);
        if (existing.has_value() && existing->size() > 0) {
            playerNBT.insert("EnderItems", std::move(*existing));
        }
    }

    // Serialize NBT to in-memory buffer
    std::stringstream nbt_stream(std::ios::in | std::ios::out | std::ios::binary);
    nbt::io::write_tag("", playerNBT, nbt_stream);
    std::string uncompressed = nbt_stream.str();

    // Gzip compress
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        LogManager::log("deflateInit2 failed while saving player data", LogManager::Error);
        return;
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(uncompressed.data()));
    zs.avail_in = static_cast<uInt>(uncompressed.size());

    int ret;
    char outbuffer[32768];
    std::string compressed;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = deflate(&zs, Z_FINISH);
        if (zs.avail_out < sizeof(outbuffer)) {
            compressed.append(outbuffer, sizeof(outbuffer) - zs.avail_out);
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        LogManager::log("Gzip compression failed while saving player data", LogManager::Error);
        return;
    }

    QDir dir;
    if (!dir.exists(playerdataDir)) {
        dir.mkpath(playerdataDir);
    }

    QString filePath = playerdataDir + "/" + data.uuid + ".dat";
    std::ofstream file(filePath.toStdString(), std::ios::binary);
    if (!file.is_open()) {
        LogManager::log(QString("Failed to open player data file for writing: %1").arg(filePath), LogManager::Error);
        return;
    }

    file.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    file.close();
}
