#include "WorldExporter.h"
#include "NBTSerializer.h"
#include "RegionFile.h"
#include "logging/LogManager.h"
#include <QDir>
#include <QFile>
#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <io/ozlibstream.h>
#include <nbt_tags.h>
#include <fstream>
#include <sstream>
#include <zlib.h>
#include <chrono>

bool WorldExporter::usesNewWorldLayout(int dataVersion) {
    return dataVersion >= NBTSerializer::DATA_VERSION_26_1;
}

QString WorldExporter::getDimensionPath(const QString& worldPath, const QString& dimension, int dataVersion) {
    if (usesNewWorldLayout(dataVersion)) {
        if (dimension == "minecraft:the_nether")
            return worldPath + "/dimensions/minecraft/the_nether";
        if (dimension == "minecraft:the_end")
            return worldPath + "/dimensions/minecraft/the_end";
        return worldPath + "/dimensions/minecraft/overworld";
    } else {
        if (dimension == "minecraft:the_nether")
            return worldPath + "/DIM-1";
        if (dimension == "minecraft:the_end")
            return worldPath + "/DIM1";
        return worldPath;
    }
}

QString WorldExporter::getPlayerDataPath(const QString& worldPath, int dataVersion) {
    return usesNewWorldLayout(dataVersion) ? worldPath + "/players/data" : worldPath + "/playerdata";
}

// Writes a gzip-compressed NBT compound to a file. Returns true on success.
static bool writeNBTFile(const QString& filePath, nbt::tag_compound root) {
    std::stringstream nbt_stream(std::ios::in | std::ios::out | std::ios::binary);
    nbt::io::write_tag("", root, nbt_stream);
    std::string uncompressed = nbt_stream.str();

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        LogManager::log(QString("deflateInit2 failed writing %1").arg(filePath), LogManager::Error);
        return false;
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
        if (zs.avail_out < sizeof(outbuffer))
            compressed.append(outbuffer, sizeof(outbuffer) - zs.avail_out);
    } while (ret == Z_OK);
    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        LogManager::log(QString("zlib deflate failed writing %1").arg(filePath), LogManager::Error);
        return false;
    }

    std::ofstream file(filePath.toStdString(), std::ios::binary);
    if (!file.is_open()) {
        LogManager::log(QString("Failed to open %1 for writing").arg(filePath), LogManager::Error);
        return false;
    }
    file.write(compressed.data(), compressed.size());
    return true;
}

bool WorldExporter::exportWorld(const BotWorldData& worldData,
                                const QString& outputPath,
                                int spawnX, int spawnY, int spawnZ,
                                const QString& worldName,
                                const MinecraftVersion& version) {
    // 1. Create directory structure
    if (!createWorldDirectories(outputPath, version.dataVersion)) {
        LogManager::log("Failed to create world directories", LogManager::Error);
        return false;
    }

    // 2. Export all chunks to region files
    std::unordered_map<RegionPos, std::unique_ptr<RegionFile>> regionCache;

    int chunksExported = 0;
    const auto loadedChunks = worldData.getLoadedChunks();
    for (const ChunkPos& chunkPos : loadedChunks) {
        const ChunkData* chunk = worldData.getChunk(chunkPos.x, chunkPos.z);
        if (!chunk) {
            continue;
        }

        QString dimPath = getDimensionPath(outputPath, chunk->dimension, version.dataVersion);
        RegionPos regionPos = RegionPos::fromChunkPos(chunk->chunkX, chunk->chunkZ);

        RegionFile* regionFile = getRegionFile(dimPath, regionPos.x, regionPos.z, regionCache);
        if (!regionFile) {
            LogManager::log(QString("Failed to get region file for region (%1, %2)").arg(regionPos.x).arg(regionPos.z), LogManager::Warning);
            continue;
        }

        nbt::tag_compound chunkNBT = NBTSerializer::chunkToNBT(*chunk, version.dataVersion);

        int localX = chunk->chunkX & 31;
        int localZ = chunk->chunkZ & 31;
        if (!regionFile->writeChunk(localX, localZ, chunkNBT)) {
            LogManager::log(QString("Failed to write chunk (%1, %2)").arg(chunk->chunkX).arg(chunk->chunkZ), LogManager::Warning);
            continue;
        }
        chunksExported++;
    }

    LogManager::log(QString("Exported %1 chunks to %2 region files").arg(chunksExported).arg(regionCache.size()), LogManager::Success);

    for (auto& pair : regionCache) {
        pair.second->flush();
    }

    // 3. Create level.dat with void generator
    if (!createLevelDat(outputPath, spawnX, spawnY, spawnZ, worldName, version)) {
        LogManager::log("Failed to create level.dat", LogManager::Error);
        return false;
    }

    // 4. Create session.lock
    if (!createSessionLock(outputPath)) {
        LogManager::log("Failed to create session.lock", LogManager::Warning);
        return false;
    }

    LogManager::log(QString("World export completed successfully to %1").arg(outputPath), LogManager::Success);
    return true;
}

bool WorldExporter::exportChunk(const ChunkData& chunk, const QString& outputPath, int dataVersion,
                                const QVector<BlockEntityData>& blockEntities) {
    RegionPos regionPos = RegionPos::fromChunkPos(chunk.chunkX, chunk.chunkZ);

    QString regionPath = getRegionFilePath(outputPath, regionPos.x, regionPos.z);
    RegionFile regionFile(regionPath);

    if (!regionFile.isValid()) {
        return false;
    }

    nbt::tag_compound chunkNBT = NBTSerializer::chunkToNBT(chunk, dataVersion, blockEntities);

    int localX = chunk.chunkX & 31;
    int localZ = chunk.chunkZ & 31;

    return regionFile.writeChunk(localX, localZ, chunkNBT);
}

bool WorldExporter::exportEntityChunk(int chunkX, int chunkZ, const QString& dimension,
                                       const QVector<EntityData>& entities,
                                       const QString& worldPath, int dataVersion) {
    QString dimPath = getDimensionPath(worldPath, dimension, dataVersion);
    QString entitiesDir = dimPath + "/entities";

    RegionPos regionPos = RegionPos::fromChunkPos(chunkX, chunkZ);
    QString regionPath = QString("%1/r.%2.%3.mca").arg(entitiesDir).arg(regionPos.x).arg(regionPos.z);
    RegionFile regionFile(regionPath);

    if (!regionFile.isValid()) {
        return false;
    }

    nbt::tag_compound entityNBT = NBTSerializer::entitiesToNBT(chunkX, chunkZ, entities, dataVersion);

    int localX = chunkX & 31;
    int localZ = chunkZ & 31;

    return regionFile.writeChunk(localX, localZ, entityNBT);
}

std::tuple<int, int, int, int> WorldExporter::getChunkBounds(const BotWorldData& worldData) {
    QVector<ChunkPos> chunks = worldData.getLoadedChunks();

    if (chunks.isEmpty()) {
        return {0, 0, 0, 0};
    }

    int minX = chunks[0].x;
    int maxX = chunks[0].x;
    int minZ = chunks[0].z;
    int maxZ = chunks[0].z;

    for (const ChunkPos& pos : std::as_const(chunks)) {
        minX = std::min(minX, pos.x);
        maxX = std::max(maxX, pos.x);
        minZ = std::min(minZ, pos.z);
        maxZ = std::max(maxZ, pos.z);
    }

    return {minX, maxX, minZ, maxZ};
}

bool WorldExporter::exportMapData(int32_t mapId, const MapData& data, const QString& worldPath, int dataVersion) {
    QString filePath;
    if (usesNewWorldLayout(dataVersion)) {
        QDir dir;
        dir.mkpath(worldPath + "/data/minecraft/maps");
        filePath = QString("%1/data/minecraft/maps/%2.dat").arg(worldPath).arg(mapId);
    } else {
        QDir dir;
        dir.mkpath(worldPath + "/data");
        filePath = QString("%1/data/map_%2.dat").arg(worldPath).arg(mapId);
    }

    nbt::tag_compound mapNbt;
    if (data.scale != 0)
        mapNbt.insert("scale", nbt::tag_byte(static_cast<int8_t>(data.scale)));
    mapNbt.insert("dimension", nbt::tag_string(data.dimension.isEmpty() ? "minecraft:overworld" : data.dimension.toStdString()));
    if (data.locked)
        mapNbt.insert("locked", nbt::tag_byte(1));
    mapNbt.insert("xCenter", nbt::tag_int(0));
    mapNbt.insert("zCenter", nbt::tag_int(0));

    std::vector<int8_t> colors(16384, 0);
    if (data.colors.size() == 16384) {
        for (int i = 0; i < 16384; ++i)
            colors[i] = static_cast<int8_t>(data.colors[i]);
    }
    mapNbt.insert("colors", nbt::tag_byte_array(std::move(colors)));

    nbt::tag_compound root;
    root.insert("DataVersion", nbt::tag_int(dataVersion));
    root.insert("data", std::move(mapNbt));

    return writeNBTFile(filePath, std::move(root));
}

bool WorldExporter::exportIdCounts(int32_t maxMapId, const QString& worldPath, int dataVersion) {
    if (maxMapId < 0) return true;

    QString filePath;
    if (usesNewWorldLayout(dataVersion)) {
        QDir dir;
        dir.mkpath(worldPath + "/data/minecraft/maps");
        filePath = worldPath + "/data/minecraft/maps/last_id.dat";
    } else {
        QDir dir;
        dir.mkpath(worldPath + "/data");
        filePath = worldPath + "/data/idcounts.dat";
    }

    nbt::tag_compound countsData;
    countsData.insert("map", nbt::tag_int(maxMapId));

    nbt::tag_compound root;
    root.insert("DataVersion", nbt::tag_int(dataVersion));
    root.insert("data", std::move(countsData));

    return writeNBTFile(filePath, std::move(root));
}

int32_t WorldExporter::readMaxMapId(const QString& worldPath, int dataVersion) {
    QString filePath = usesNewWorldLayout(dataVersion)
        ? worldPath + "/data/minecraft/maps/last_id.dat"
        : worldPath + "/data/idcounts.dat";

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    QByteArray compressed = f.readAll();
    f.close();

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return -1;

    zs.next_in = reinterpret_cast<Bytef*>(compressed.data());
    zs.avail_in = static_cast<uInt>(compressed.size());

    std::string decompressed;
    char buf[32768];
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return -1; }
        decompressed.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    inflateEnd(&zs);

    try {
        std::istringstream ss(decompressed, std::ios::binary);
        auto [name, rootPtr] = nbt::io::read_compound(ss);
        if (!rootPtr) return -1;
        auto& root = *rootPtr;
        if (root.has_key("data", nbt::tag_type::Compound)) {
            auto& data = root.at("data").as<nbt::tag_compound>();
            if (data.has_key("map", nbt::tag_type::Int))
                return static_cast<int32_t>(data.at("map").as<nbt::tag_int>());
        }
    } catch (...) {}

    return -1;
}

static void migrateToNewWorldLayout(const QString& worldPath) {
    QDir dir(worldPath);

    bool hasOldLayout = dir.exists("region") || dir.exists("playerdata") || dir.exists("DIM-1") || dir.exists("DIM1");
    bool hasNewLayout = dir.exists("dimensions");

    if (!hasOldLayout || hasNewLayout)
        return;

    LogManager::log(QString("Migrating world save at %1 from pre-26.1 to 26.1+ layout").arg(worldPath), LogManager::Info);

    dir.mkpath("dimensions/minecraft/overworld");
    dir.mkpath("dimensions/minecraft/the_nether");
    dir.mkpath("dimensions/minecraft/the_end");

    if (dir.exists("region"))
        dir.rename("region", "dimensions/minecraft/overworld/region");
    if (dir.exists("entities"))
        dir.rename("entities", "dimensions/minecraft/overworld/entities");

    if (dir.exists("DIM-1/region"))
        dir.rename("DIM-1/region", "dimensions/minecraft/the_nether/region");
    if (dir.exists("DIM-1/entities"))
        dir.rename("DIM-1/entities", "dimensions/minecraft/the_nether/entities");
    dir.rmdir("DIM-1");

    if (dir.exists("DIM1/region"))
        dir.rename("DIM1/region", "dimensions/minecraft/the_end/region");
    if (dir.exists("DIM1/entities"))
        dir.rename("DIM1/entities", "dimensions/minecraft/the_end/entities");
    dir.rmdir("DIM1");

    if (dir.exists("playerdata")) {
        dir.mkpath("players");
        dir.rename("playerdata", "players/data");
    }

    // Map data files
    QDir dataDir(worldPath + "/data");
    if (dataDir.exists()) {
        dir.mkpath("data/minecraft/maps");
        const QStringList mapFiles = dataDir.entryList(QStringList() << "map_*.dat", QDir::Files);
        for (const QString& filename : mapFiles) {
            // Strip "map_" prefix (4 chars) and ".dat" suffix (4 chars)
            QString numStr = filename.mid(4, filename.length() - 8);
            dir.rename("data/" + filename, "data/minecraft/maps/" + numStr + ".dat");
        }
        if (dataDir.exists("idcounts.dat"))
            dir.rename("data/idcounts.dat", "data/minecraft/maps/last_id.dat");
    }

    LogManager::log(QString("World layout migration complete for %1").arg(worldPath), LogManager::Success);
}

bool WorldExporter::createWorldDirectories(const QString& outputPath, int dataVersion) {
    QDir dir;

    if (!dir.mkpath(outputPath)) {
        return false;
    }

    if (usesNewWorldLayout(dataVersion)) {
        migrateToNewWorldLayout(outputPath);

        dir.mkpath(outputPath + "/dimensions/minecraft/overworld/region");
        dir.mkpath(outputPath + "/dimensions/minecraft/overworld/entities");
        dir.mkpath(outputPath + "/dimensions/minecraft/the_nether/entities");
        dir.mkpath(outputPath + "/dimensions/minecraft/the_end/entities");
        dir.mkpath(outputPath + "/players/data");
        dir.mkpath(outputPath + "/data/minecraft/maps");
    } else {
        if (!dir.mkpath(outputPath + "/region")) {
            return false;
        }
        dir.mkpath(outputPath + "/playerdata");
        dir.mkpath(outputPath + "/data");
        dir.mkpath(outputPath + "/entities");
        dir.mkpath(outputPath + "/DIM-1/entities");
        dir.mkpath(outputPath + "/DIM1/entities");
    }

    return true;
}

int WorldExporter::readLevelDatDataVersion(const QString& worldPath) {
    QFile f(worldPath + "/level.dat");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QByteArray compressed = f.readAll();
    f.close();

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return 0;

    zs.next_in = reinterpret_cast<Bytef*>(compressed.data());
    zs.avail_in = static_cast<uInt>(compressed.size());

    std::string decompressed;
    char buf[32768];
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return 0; }
        decompressed.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    inflateEnd(&zs);

    try {
        std::istringstream ss(decompressed, std::ios::binary);
        auto [name, rootPtr] = nbt::io::read_compound(ss);
        if (!rootPtr) return 0;
        auto& root = *rootPtr;
        if (root.has_key("Data", nbt::tag_type::Compound)) {
            auto& data = root.at("Data").as<nbt::tag_compound>();
            if (data.has_key("DataVersion", nbt::tag_type::Int)) {
                return static_cast<int>(data.at("DataVersion").as<nbt::tag_int>());
            }
        }
    } catch (...) {}

    return 0;
}

bool WorldExporter::createLevelDat(const QString& outputPath,
                                  int spawnX, int spawnY, int spawnZ,
                                  const QString& worldName,
                                  const MinecraftVersion& version) {
    nbt::tag_compound data;

    data.insert("version", nbt::tag_int(19133));  // Anvil format version
    data.insert("DataVersion", nbt::tag_int(version.dataVersion));

    nbt::tag_compound versionInfo;
    versionInfo.insert("Id", nbt::tag_int(version.dataVersion));
    versionInfo.insert("Name", nbt::tag_string(version.versionName.toStdString()));
    versionInfo.insert("Series", nbt::tag_string(version.series.toStdString()));
    versionInfo.insert("Snapshot", nbt::tag_byte(version.isSnapshot ? 1 : 0));
    data.insert("Version", std::move(versionInfo));

    data.insert("LevelName", nbt::tag_string(worldName.toStdString()));

    nbt::tag_compound spawnCompound;
    spawnCompound.insert("pos", nbt::tag_int_array(std::vector<int32_t>{spawnX, spawnY, spawnZ}));
    spawnCompound.insert("pitch", nbt::tag_float(0.0f));
    spawnCompound.insert("yaw", nbt::tag_float(0.0f));
    spawnCompound.insert("dimension", nbt::tag_string("minecraft:overworld"));
    data.insert("spawn", std::move(spawnCompound));

    if (usesNewWorldLayout(version.dataVersion)) {
        // 26.1+: difficulty moved to difficulty_settings compound (string value)
        nbt::tag_compound diffSettings;
        diffSettings.insert("difficulty", nbt::tag_string("normal"));
        diffSettings.insert("locked", nbt::tag_byte(0));
        data.insert("difficulty_settings", std::move(diffSettings));
    } else {
        data.insert("Difficulty", nbt::tag_byte(2));  // Normal
        data.insert("DifficultyLocked", nbt::tag_byte(0));
    }

    data.insert("GameType", nbt::tag_int(1));  // Creative
    data.insert("hardcore", nbt::tag_byte(0));
    data.insert("allowCommands", nbt::tag_byte(1));

    nbt::tag_compound dimensions;
    dimensions.insert("minecraft:overworld", createVoidDimension("minecraft:overworld"));
    dimensions.insert("minecraft:the_nether", createVoidDimension("minecraft:the_nether"));
    dimensions.insert("minecraft:the_end", createVoidDimension("minecraft:the_end"));

    if (usesNewWorldLayout(version.dataVersion)) {
        // 26.1+: WorldGenSettings moves to data/minecraft/world_gen_settings.dat
        // MC SavedData format: root has "DataVersion" + "data" compound with actual fields
        // generate_features renamed to generate_structures
        nbt::tag_compound worldGenData;
        worldGenData.insert("bonus_chest", nbt::tag_byte(0));
        worldGenData.insert("generate_structures", nbt::tag_byte(0));
        worldGenData.insert("seed", nbt::tag_long(0));
        worldGenData.insert("dimensions", std::move(dimensions));
        nbt::tag_compound worldGenRoot;
        worldGenRoot.insert("DataVersion", nbt::tag_int(version.dataVersion));
        worldGenRoot.insert("data", std::move(worldGenData));
        writeNBTFile(outputPath + "/data/minecraft/world_gen_settings.dat", std::move(worldGenRoot));

        // 26.1+: weather moves to data/minecraft/weather.dat with renamed keys
        // MC SavedData format: root has "DataVersion" + "data" compound with actual fields
        nbt::tag_compound weatherData;
        weatherData.insert("raining", nbt::tag_byte(0));
        weatherData.insert("rain_time", nbt::tag_int(2147483647));
        weatherData.insert("thundering", nbt::tag_byte(0));
        weatherData.insert("thunder_time", nbt::tag_int(2147483647));
        weatherData.insert("clear_weather_time", nbt::tag_int(2147483647));
        nbt::tag_compound weatherRoot;
        weatherRoot.insert("DataVersion", nbt::tag_int(version.dataVersion));
        weatherRoot.insert("data", std::move(weatherData));
        writeNBTFile(outputPath + "/data/minecraft/weather.dat", std::move(weatherRoot));
    } else {
        nbt::tag_compound worldGen;
        worldGen.insert("bonus_chest", nbt::tag_byte(0));
        worldGen.insert("generate_features", nbt::tag_byte(0));  // No structures
        worldGen.insert("seed", nbt::tag_long(0));
        worldGen.insert("dimensions", std::move(dimensions));
        data.insert("WorldGenSettings", std::move(worldGen));

        data.insert("raining", nbt::tag_byte(0));
        data.insert("rainTime", nbt::tag_int(2147483647));
        data.insert("thundering", nbt::tag_byte(0));
        data.insert("thunderTime", nbt::tag_int(2147483647));
        data.insert("clearWeatherTime", nbt::tag_int(2147483647));
    }

    data.insert("Time", nbt::tag_long(6000));  // Noon
    data.insert("DayTime", nbt::tag_long(6000));

    data.insert("BorderCenterX", nbt::tag_double(0.0));
    data.insert("BorderCenterZ", nbt::tag_double(0.0));
    data.insert("BorderSize", nbt::tag_double(60000000.0));
    data.insert("BorderSizeLerpTarget", nbt::tag_double(60000000.0));
    data.insert("BorderSizeLerpTime", nbt::tag_long(0));
    data.insert("BorderWarningBlocks", nbt::tag_double(5.0));
    data.insert("BorderWarningTime", nbt::tag_double(15.0));
    data.insert("BorderDamagePerBlock", nbt::tag_double(0.2));
    data.insert("BorderSafeZone", nbt::tag_double(5.0));

    data.insert("initialized", nbt::tag_byte(1));
    data.insert("WasModded", nbt::tag_byte(0));

    nbt::tag_compound dataPacks;
    nbt::tag_list enabledPacks(nbt::tag_type::String);
    enabledPacks.push_back(nbt::tag_string("vanilla"));
    dataPacks.insert("Enabled", std::move(enabledPacks));
    nbt::tag_list disabledPacks(nbt::tag_type::String);
    if (!usesNewWorldLayout(version.dataVersion)) {
        disabledPacks.push_back(nbt::tag_string("minecart_improvements"));
        disabledPacks.push_back(nbt::tag_string("redstone_experiments"));
        disabledPacks.push_back(nbt::tag_string("trade_rebalance"));
    }
    dataPacks.insert("Disabled", std::move(disabledPacks));
    data.insert("DataPacks", std::move(dataPacks));

    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    data.insert("LastPlayed", nbt::tag_long(millis));

    nbt::tag_compound root;
    root.insert("Data", std::move(data));

    if (!writeNBTFile(outputPath + "/level.dat", std::move(root))) {
        return false;
    }

    LogManager::log(QString("level.dat created successfully"), LogManager::Info);
    return true;
}

bool WorldExporter::createSessionLock(const QString& outputPath) {
    QString lockPath = outputPath + "/session.lock";
    QFile file(lockPath);

    if (file.open(QIODevice::WriteOnly)) {
        file.close();
        return true;
    }

    return false;
}

RegionFile* WorldExporter::getRegionFile(const QString& outputPath,
                                        int regionX, int regionZ,
                                        std::unordered_map<RegionPos, std::unique_ptr<RegionFile>>& regionCache) {
    RegionPos pos(regionX, regionZ);

    auto it = regionCache.find(pos);
    if (it != regionCache.end()) {
        return it->second.get();
    }

    QString regionPath = getRegionFilePath(outputPath, regionX, regionZ);
    auto regionFile = std::make_unique<RegionFile>(regionPath);

    if (!regionFile->isValid()) {
        return nullptr;
    }

    RegionFile* ptr = regionFile.get();
    regionCache[pos] = std::move(regionFile);

    return ptr;
}

QString WorldExporter::getRegionFilePath(const QString& outputPath, int regionX, int regionZ) {
    return QString("%1/region/r.%2.%3.mca").arg(outputPath).arg(regionX).arg(regionZ);
}

nbt::tag_compound WorldExporter::createVoidDimension(const std::string& dimensionType) {
    std::string biome = "minecraft:the_void";

    nbt::tag_compound settings;
    settings.insert("biome", nbt::tag_string(biome));
    settings.insert("layers", nbt::tag_list(nbt::tag_type::Compound));  // empty = void
    settings.insert("features", nbt::tag_byte(0));
    settings.insert("lake", nbt::tag_byte(0));

    nbt::tag_compound generator;
    generator.insert("type", nbt::tag_string("minecraft:flat"));
    generator.insert("settings", std::move(settings));

    nbt::tag_compound dim;
    dim.insert("type", nbt::tag_string(dimensionType));
    dim.insert("generator", std::move(generator));

    return dim;
}
