#include "ChunkSavingWorker.h"
#include "world/WorldExporter.h"
#include "world/RegionFile.h"
#include "world/NBTSerializer.h"
#include "logging/LogManager.h"
#include <QDir>
#include <QFile>
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

ChunkSavingWorker::ChunkSavingWorker(QObject *parent) : QObject(parent) {}
ChunkSavingWorker::~ChunkSavingWorker() = default;

void ChunkSavingWorker::processChunk(const ChunkData& chunk, const QVector<BlockEntityData>& blockEntities,
                                      const QString& worldPath, int dataVersion) {
    // Determine dimension path
    QString dimensionPath;
    QString dimensionName;
    if (chunk.dimension == "minecraft:the_nether") {
        dimensionPath = worldPath + "/DIM-1";
        dimensionName = "Nether";
    } else if (chunk.dimension == "minecraft:overworld") {
        dimensionPath = worldPath;
        dimensionName = "Overworld";
    } else if (chunk.dimension == "minecraft:the_end") {
        dimensionPath = worldPath + "/DIM1";
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
        if (!be.rawNbt.isEmpty() && be.items.isEmpty()) {
            hasUnknownContainers = true;
            break;
        }
    }

    QVector<BlockEntityData> effectiveBEs = blockEntities;
    if (hasUnknownContainers) {
        int regionX = chunk.chunkX >> 5;
        int regionZ = chunk.chunkZ >> 5;
        QString regionPath = QString("%1/region/r.%2.%3.mca").arg(dimensionPath).arg(regionX).arg(regionZ);

        if (QFile::exists(regionPath)) {
            RegionFile existingRegion(regionPath);
            if (existingRegion.isValid()) {
                nbt::tag_compound existingChunk = existingRegion.readChunk(chunk.chunkX & 31, chunk.chunkZ & 31);

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

    WorldExporter::exportChunk(chunk, dimensionPath, dataVersion, effectiveBEs);
}

void ChunkSavingWorker::processEntityChunk(int chunkX, int chunkZ, const QString& dimension,
                                            const QVector<EntityData>& entities,
                                            const QString& worldPath, int dataVersion) {
    // Determine entities directory
    QString entitiesDir;
    if (dimension == "minecraft:the_nether") {
        entitiesDir = worldPath + "/DIM-1/entities";
    } else if (dimension == "minecraft:overworld") {
        entitiesDir = worldPath + "/entities";
    } else if (dimension == "minecraft:the_end") {
        entitiesDir = worldPath + "/DIM1/entities";
    } else {
        LogManager::log(QString("Cannot save entities with unknown dimension: %1").arg(dimension), LogManager::Warning);
        return;
    }

    QDir dir;
    if (!dir.exists(entitiesDir)) {
        dir.mkpath(entitiesDir);
    }

    WorldExporter::exportEntityChunk(chunkX, chunkZ, dimension, entities, worldPath, dataVersion);
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
    if (data.enderItems.isEmpty()) {
        QString filePath = worldPath + "/playerdata/" + data.uuid + ".dat";
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

    // Write to playerdata/{uuid}.dat
    QString playerdataDir = worldPath + "/playerdata";
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
