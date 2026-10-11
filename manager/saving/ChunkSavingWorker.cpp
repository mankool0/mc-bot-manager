#include "ChunkSavingWorker.h"
#include "world/WorldExporter.h"
#include "world/RegionFile.h"
#include "world/NBTSerializer.h"
#include "logging/LogManager.h"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
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

static const QString kEntityDimensions[] = {
    QStringLiteral("minecraft:overworld"), QStringLiteral("minecraft:the_nether"), QStringLiteral("minecraft:the_end"),
};

static int entityDimensionIndex(const QString& dimension) {
    const auto it = std::find(std::begin(kEntityDimensions), std::end(kEntityDimensions), dimension);
    return it == std::end(kEntityDimensions) ? -1 : static_cast<int>(it - std::begin(kEntityDimensions));
}

static QUuid uuidFromNbt(const nbt::tag_compound& entity) {
    if (!entity.has_key("UUID", nbt::tag_type::Int_Array)) return {};
    const auto& ints = static_cast<const nbt::tag_int_array&>(entity.at("UUID").get());
    if (ints.size() != 4) return {};
    char bytes[16];
    for (int i = 0; i < 4; ++i) {
        qToBigEndian<qint32>(ints.at(i), bytes + i * 4);
    }
    return QUuid::fromRfc4122(QByteArrayView(bytes, sizeof(bytes)));
}

// Entity index file: the magic, then records appended per flush. A set record is
// 1, the uuid (16 bytes), dimension (1), chunk x and z (4 each, big-endian); a removal is 0, the uuid.
static const QByteArray kEntityIndexMagic = QByteArrayLiteral("MCBMEIX1");
static constexpr int kEntityIndexSetSize = 26;
static constexpr int kEntityIndexRemoveSize = 17;

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

void ChunkSavingWorker::closeRegion(const QString& path) {
    m_openRegions.erase(std::remove_if(m_openRegions.begin(), m_openRegions.end(),
                                       [&path](const auto& open) { return open.first == path; }),
                        m_openRegions.end());
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

void ChunkSavingWorker::processEntityFlush(const QHash<DimChunkPos, QVector<EntityData>>& chunks,
                                            const QSet<QString>& dead) {
    if (!m_entityIndexLoaded) {
        loadEntityIndex();
    }

    QSet<DimChunkPos> rewrite;
    for (auto chunk = chunks.cbegin(); chunk != chunks.cend(); ++chunk) {
        rewrite.insert(chunk.key());
        const int dimension = entityDimensionIndex(chunk.key().dimension);
        if (dimension < 0) continue;
        const EntityChunkRef here{chunk.key().chunkX, chunk.key().chunkZ, static_cast<quint8>(dimension)};
        for (const EntityData& entity : chunk.value()) {
            const QUuid uuid = QUuid::fromString(entity.uuid);
            if (uuid.isNull()) continue;
            auto saved = m_entityIndex.constFind(uuid);
            if (saved != m_entityIndex.constEnd()) {
                if (*saved == here) continue;
                rewrite.insert({kEntityDimensions[saved->dimension], saved->chunkX, saved->chunkZ});
            }
            setEntityLocation(uuid, &here);
        }
    }

    QSet<QUuid> deadUuids;
    for (const QString& id : dead) {
        const QUuid uuid = QUuid::fromString(id);
        deadUuids.insert(uuid);
        auto saved = m_entityIndex.constFind(uuid);
        if (saved != m_entityIndex.constEnd()) {
            rewrite.insert({kEntityDimensions[saved->dimension], saved->chunkX, saved->chunkZ});
            setEntityLocation(uuid, nullptr);
        }
    }

    for (const DimChunkPos& pos : std::as_const(rewrite)) {
        rewriteEntityChunk(pos, chunks.value(pos), deadUuids);
    }
    saveEntityIndex();
}

void ChunkSavingWorker::rewriteEntityChunk(const DimChunkPos& pos, const QVector<EntityData>& entities,
                                           const QSet<QUuid>& dead) {
    const int dimension = entityDimensionIndex(pos.dimension);
    if (dimension < 0) {
        LogManager::log(QString("Cannot save entities with unknown dimension: %1").arg(pos.dimension), LogManager::Warning);
        return;
    }
    const EntityChunkRef here{pos.chunkX, pos.chunkZ, static_cast<quint8>(dimension)};

    const QString path = QString("%1/entities/r.%2.%3.mca")
        .arg(WorldExporter::getDimensionPath(m_worldPath, pos.dimension, m_dataVersion))
        .arg(pos.chunkX >> 5).arg(pos.chunkZ >> 5);
    if (entities.isEmpty() && !QFile::exists(path)) return;
    RegionFile* region = openRegion(path);
    if (!region) {
        LogManager::log(QString("Failed to open entity region %1 for chunk (%2, %3)")
                       .arg(path).arg(pos.chunkX).arg(pos.chunkZ), LogManager::Error);
        return;
    }

    QSet<QUuid> written;
    for (const EntityData& entity : entities) {
        written.insert(QUuid::fromString(entity.uuid));
    }

    const int localX = pos.chunkX & 31;
    const int localZ = pos.chunkZ & 31;
    nbt::tag_list kept(nbt::tag_type::Compound);
    size_t onDisk = 0;
    const bool hadRecord = region->hasChunk(localX, localZ);
    if (hadRecord) {
        nbt::tag_compound record = region->readChunk(localX, localZ);
        if (record.has_key("Entities", nbt::tag_type::List)) {
            auto& list = static_cast<nbt::tag_list&>(record.at("Entities").get());
            if (list.el_type() == nbt::tag_type::Compound) {
                onDisk = list.size();
                for (auto& value : list) {
                    auto& entity = static_cast<nbt::tag_compound&>(value.get());
                    const QUuid uuid = uuidFromNbt(entity);
                    if (!uuid.isNull()) {
                        if (dead.contains(uuid) || written.contains(uuid)) continue;
                        auto saved = m_entityIndex.constFind(uuid);
                        if (saved == m_entityIndex.constEnd()) {
                            // Saved before the index knew it; from now on this is its copy.
                            setEntityLocation(uuid, &here);
                        } else if (!(*saved == here)) {
                            continue;
                        }
                    }
                    kept.push_back(std::move(entity));
                }
            }
        }
    }

    // Like vanilla, an emptied chunk loses its record: an empty list still costs a 4 KiB sector,
    // and a region with nothing left costs its 8 KiB header.
    if (entities.isEmpty() && kept.size() == 0) {
        if (!hadRecord) return;
        region->removeChunk(localX, localZ);
        if (region->isEmpty()) {
            closeRegion(path);
            QFile::remove(path);
        } else {
            region->flush();
        }
        return;
    }
    if (entities.isEmpty() && kept.size() == onDisk) return;
    WorldExporter::exportEntityChunk(pos.chunkX, pos.chunkZ, entities, *region, m_dataVersion, std::move(kept));
    region->flush();
}

void ChunkSavingWorker::setEntityLocation(const QUuid& uuid, const EntityChunkRef* where) {
    m_entityIndexPending.append(char(where ? 1 : 0));
    m_entityIndexPending.append(uuid.toRfc4122());
    if (where) {
        m_entityIndex.insert(uuid, *where);
        char chunk[8];
        qToBigEndian<qint32>(where->chunkX, chunk);
        qToBigEndian<qint32>(where->chunkZ, chunk + 4);
        m_entityIndexPending.append(char(where->dimension));
        m_entityIndexPending.append(chunk, sizeof(chunk));
    } else {
        m_entityIndex.remove(uuid);
    }
    ++m_entityIndexPendingRecords;
}

void ChunkSavingWorker::loadEntityIndex() {
    m_entityIndexLoaded = true;
    QFile file(m_worldPath + "/mcbm_entity_index.bin");
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray data = file.readAll();

    bool damaged = !data.startsWith(kEntityIndexMagic);
    const char* p = data.constData() + kEntityIndexMagic.size();
    const char* end = data.constData() + data.size();
    while (!damaged && p < end) {
        const bool set = *p == 1;
        if ((*p != 0 && !set) || end - p < (set ? kEntityIndexSetSize : kEntityIndexRemoveSize)) {
            damaged = true;
            break;
        }
        const QUuid uuid = QUuid::fromRfc4122(QByteArrayView(p + 1, 16));
        if (set) {
            const quint8 dimension = static_cast<quint8>(p[17]);
            if (dimension >= std::size(kEntityDimensions)) {
                damaged = true;
                break;
            }
            m_entityIndex.insert(uuid, {qFromBigEndian<qint32>(p + 18), qFromBigEndian<qint32>(p + 22), dimension});
            p += kEntityIndexSetSize;
        } else {
            m_entityIndex.remove(uuid);
            p += kEntityIndexRemoveSize;
        }
        ++m_entityIndexFileRecords;
    }

    // A crash mid-append leaves a partial record, and appending after it would misalign the rest.
    if (damaged) {
        LogManager::log(QString("Entity index for %1 is damaged; keeping the %2 entries read before the damage")
                       .arg(m_worldPath).arg(m_entityIndex.size()), LogManager::Warning);
        compactEntityIndex();
    }
}

void ChunkSavingWorker::saveEntityIndex() {
    if (m_entityIndexPendingRecords == 0) return;
    if (m_entityIndexFileRecords + m_entityIndexPendingRecords > 2 * m_entityIndex.size() + 4096) {
        compactEntityIndex();
        return;
    }

    QFile file(m_worldPath + "/mcbm_entity_index.bin");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        LogManager::log(QString("Failed to append to the entity index for %1: %2")
                       .arg(m_worldPath, file.errorString()), LogManager::Error);
        return;
    }
    if (file.size() == 0) {
        file.write(kEntityIndexMagic);
    }
    if (file.write(m_entityIndexPending) != m_entityIndexPending.size()) {
        LogManager::log(QString("Failed to append to the entity index for %1: %2")
                       .arg(m_worldPath, file.errorString()), LogManager::Error);
        file.close();
        // The tail is now partial; the next load would stop there, so start the file over.
        compactEntityIndex();
        return;
    }
    m_entityIndexFileRecords += m_entityIndexPendingRecords;
    m_entityIndexPending.clear();
    m_entityIndexPendingRecords = 0;
}

void ChunkSavingWorker::compactEntityIndex() {
    QByteArray data = kEntityIndexMagic;
    data.reserve(kEntityIndexMagic.size() + m_entityIndex.size() * kEntityIndexSetSize);
    char chunk[8];
    for (auto it = m_entityIndex.cbegin(); it != m_entityIndex.cend(); ++it) {
        data.append(char(1));
        data.append(it.key().toRfc4122());
        data.append(char(it->dimension));
        qToBigEndian<qint32>(it->chunkX, chunk);
        qToBigEndian<qint32>(it->chunkZ, chunk + 4);
        data.append(chunk, sizeof(chunk));
    }

    QSaveFile file(m_worldPath + "/mcbm_entity_index.bin");
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        LogManager::log(QString("Failed to write the entity index for %1: %2")
                       .arg(m_worldPath, file.errorString()), LogManager::Error);
        return;
    }
    m_entityIndexFileRecords = m_entityIndex.size();
    m_entityIndexPending.clear();
    m_entityIndexPendingRecords = 0;
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
