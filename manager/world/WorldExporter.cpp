#include "WorldExporter.h"
#include "NBTSerializer.h"
#include "RegionFile.h"
#include "logging/LogManager.h"
#include <QDir>
#include <QFile>
#include <io/stream_writer.h>
#include <io/ozlibstream.h>
#include <fstream>
#include <sstream>
#include <zlib.h>
#include <chrono>

bool WorldExporter::exportWorld(const BotWorldData& worldData,
                                const QString& outputPath,
                                int spawnX, int spawnY, int spawnZ,
                                const QString& worldName,
                                const MinecraftVersion& version) {
    // 1. Create directory structure
    if (!createWorldDirectories(outputPath)) {
        LogManager::log("Failed to create world directories", LogManager::Error);
        return false;
    }

    // 2. Export all chunks to region files
    std::unordered_map<RegionPos, std::unique_ptr<RegionFile>> regionCache;

    int chunksExported = 0;
    for (const ChunkPos& chunkPos : worldData.getLoadedChunks()) {
        const ChunkData* chunk = worldData.getChunk(chunkPos.x, chunkPos.z);
        if (!chunk) {
            continue;
        }

        // Calculate region coordinates
        RegionPos regionPos = RegionPos::fromChunkPos(chunk->chunkX, chunk->chunkZ);

        // Get or create region file
        RegionFile* regionFile = getRegionFile(outputPath, regionPos.x, regionPos.z, regionCache);
        if (!regionFile) {
            LogManager::log(QString("Failed to get region file for region (%1, %2)").arg(regionPos.x).arg(regionPos.z), LogManager::Warning);
            continue;
        }

        // Convert chunk to NBT
        nbt::tag_compound chunkNBT = NBTSerializer::chunkToNBT(*chunk, version.dataVersion);

        // Write to region file (local coordinates within region)
        int localX = chunk->chunkX & 31;
        int localZ = chunk->chunkZ & 31;
        if (!regionFile->writeChunk(localX, localZ, chunkNBT)) {
            LogManager::log(QString("Failed to write chunk (%1, %2)").arg(chunk->chunkX).arg(chunk->chunkZ), LogManager::Warning);
            continue;
        }
        chunksExported++;
    }

    LogManager::log(QString("Exported %1 chunks to %2 region files").arg(chunksExported).arg(regionCache.size()), LogManager::Success);

    // Flush all region files
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

bool WorldExporter::exportChunk(const ChunkData& chunk, const QString& outputPath, int dataVersion) {
    // Calculate region coordinates
    RegionPos regionPos = RegionPos::fromChunkPos(chunk.chunkX, chunk.chunkZ);

    // Create region file
    QString regionPath = getRegionFilePath(outputPath, regionPos.x, regionPos.z);
    RegionFile regionFile(regionPath);

    if (!regionFile.isValid()) {
        return false;
    }

    // Convert and write
    nbt::tag_compound chunkNBT = NBTSerializer::chunkToNBT(chunk, dataVersion);

    int localX = chunk.chunkX & 31;
    int localZ = chunk.chunkZ & 31;

    return regionFile.writeChunk(localX, localZ, chunkNBT);
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

    for (const ChunkPos& pos : chunks) {
        minX = std::min(minX, pos.x);
        maxX = std::max(maxX, pos.x);
        minZ = std::min(minZ, pos.z);
        maxZ = std::max(maxZ, pos.z);
    }

    return {minX, maxX, minZ, maxZ};
}

bool WorldExporter::createWorldDirectories(const QString& outputPath) {
    QDir dir;

    // Create main directory
    if (!dir.mkpath(outputPath)) {
        return false;
    }

    // Create region directory
    if (!dir.mkpath(outputPath + "/region")) {
        return false;
    }

    // Create optional directories
    dir.mkpath(outputPath + "/playerdata");
    dir.mkpath(outputPath + "/data");

    return true;
}

bool WorldExporter::createLevelDat(const QString& outputPath,
                                  int spawnX, int spawnY, int spawnZ,
                                  const QString& worldName,
                                  const MinecraftVersion& version) {
    nbt::tag_compound data;

    // Basic world settings
    data.insert("version", nbt::tag_int(19133));  // Anvil format version
    data.insert("DataVersion", nbt::tag_int(version.dataVersion));

    // Version info compound
    nbt::tag_compound versionInfo;
    versionInfo.insert("Id", nbt::tag_int(version.dataVersion));
    versionInfo.insert("Name", nbt::tag_string(version.versionName.toStdString()));
    versionInfo.insert("Series", nbt::tag_string(version.series.toStdString()));
    versionInfo.insert("Snapshot", nbt::tag_byte(version.isSnapshot ? 1 : 0));
    data.insert("Version", std::move(versionInfo));

    data.insert("LevelName", nbt::tag_string(worldName.toStdString()));

    // Spawn coordinates
    data.insert("SpawnX", nbt::tag_int(spawnX));
    data.insert("SpawnY", nbt::tag_int(spawnY));
    data.insert("SpawnZ", nbt::tag_int(spawnZ));
    data.insert("SpawnAngle", nbt::tag_float(0.0f));

    // Disable features in new chunks
    data.insert("MapFeatures", nbt::tag_byte(0));

    // Game settings
    data.insert("Difficulty", nbt::tag_byte(2));  // Normal
    data.insert("DifficultyLocked", nbt::tag_byte(0));
    data.insert("GameType", nbt::tag_int(1));  // Creative
    data.insert("hardcore", nbt::tag_byte(0));
    data.insert("allowCommands", nbt::tag_byte(1));

    // World gen settings
    nbt::tag_compound worldGen;
    worldGen.insert("bonus_chest", nbt::tag_byte(0));
    worldGen.insert("generate_features", nbt::tag_byte(0));  // No structures
    worldGen.insert("seed", nbt::tag_long(0));

    // Dimensions with void generator
    nbt::tag_compound dimensions;
    dimensions.insert("minecraft:overworld", createVoidDimension("minecraft:overworld"));
    dimensions.insert("minecraft:the_nether", createVoidDimension("minecraft:the_nether"));
    dimensions.insert("minecraft:the_end", createVoidDimension("minecraft:the_end"));
    worldGen.insert("dimensions", std::move(dimensions));

    data.insert("WorldGenSettings", std::move(worldGen));

    // Time settings
    data.insert("Time", nbt::tag_long(6000));  // Noon
    data.insert("DayTime", nbt::tag_long(6000));

    // Weather settings - clear weather for ~3.4 years of gameplay (max int32)
    data.insert("raining", nbt::tag_byte(0));
    data.insert("rainTime", nbt::tag_int(2147483647));  // Max int32 - never rain
    data.insert("thundering", nbt::tag_byte(0));
    data.insert("thunderTime", nbt::tag_int(2147483647));  // Max int32 - never thunder
    data.insert("clearWeatherTime", nbt::tag_int(2147483647));  // Keep clear weather indefinitely

    // World border settings
    data.insert("BorderCenterX", nbt::tag_double(0.0));
    data.insert("BorderCenterZ", nbt::tag_double(0.0));
    data.insert("BorderSize", nbt::tag_double(60000000.0));
    data.insert("BorderSizeLerpTarget", nbt::tag_double(60000000.0));
    data.insert("BorderSizeLerpTime", nbt::tag_long(0));
    data.insert("BorderWarningBlocks", nbt::tag_double(5.0));
    data.insert("BorderWarningTime", nbt::tag_double(15.0));
    data.insert("BorderDamagePerBlock", nbt::tag_double(0.2));
    data.insert("BorderSafeZone", nbt::tag_double(5.0));

    // World state
    data.insert("initialized", nbt::tag_byte(1));
    data.insert("WasModded", nbt::tag_byte(0));

    // Current timestamp in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    data.insert("LastPlayed", nbt::tag_long(millis));

    // Wrap in root compound
    nbt::tag_compound root;
    root.insert("Data", std::move(data));

    // 1. Serialize NBT to an in-memory stringstream
    std::stringstream nbt_stream(std::ios::in | std::ios::out | std::ios::binary);
    nbt::io::write_tag("", root, nbt_stream);

    // 2. Get the uncompressed data as a string
    std::string uncompressed_data = nbt_stream.str();

    // 3. Prepare for zlib compression
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // Initialize for gzip encoding.
    // windowBits = 15 (max window size) + 16 (gzip header)
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        LogManager::log("deflateInit2 failed while creating level.dat", LogManager::Error);
        return false;
    }

    zs.next_in = (Bytef*)uncompressed_data.data();
    zs.avail_in = uncompressed_data.size();

    int ret;
    char outbuffer[32768];
    std::string compressed_data;

    // 4. Deflate until end of stream
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (zs.avail_out < sizeof(outbuffer)) {
            compressed_data.append(outbuffer, sizeof(outbuffer) - zs.avail_out);
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        LogManager::log(QString("Exception during zlib compression: deflate failed with code %1").arg(ret), LogManager::Error);
        return false;
    }

    // 5. Write the compressed data to the file
    QString levelPath = outputPath + "/level.dat";
    std::ofstream file(levelPath.toStdString(), std::ios::binary);
    if (!file.is_open()) {
        LogManager::log("Failed to open level.dat for writing", LogManager::Error);
        return false;
    }

    file.write(compressed_data.data(), compressed_data.size());
    file.close();

    LogManager::log(QString("level.dat created successfully (uncompressed: %1 bytes, compressed: %2 bytes)")
                   .arg(uncompressed_data.size()).arg(compressed_data.size()), LogManager::Info);
    return true;

}

bool WorldExporter::createSessionLock(const QString& outputPath) {
    QString lockPath = outputPath + "/session.lock";
    QFile file(lockPath);

    // Just create an empty file
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

    // Check if already in cache
    auto it = regionCache.find(pos);
    if (it != regionCache.end()) {
        return it->second.get();
    }

    // Create new region file
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
    nbt::tag_compound dim;

    // Type
    dim.insert("type", nbt::tag_string(dimensionType));

    // Chunk generator - flat with no layers (void)
    nbt::tag_compound generator;
    generator.insert("type", nbt::tag_string("minecraft:flat"));

    nbt::tag_compound settings;

    // Empty layers = void
    nbt::tag_list layers(nbt::tag_type::Compound);
    settings.insert("layers", std::move(layers));

    settings.insert("biome", nbt::tag_string("minecraft:plains"));

    // Structure settings
    nbt::tag_compound structures;
    structures.insert("structures", nbt::tag_compound());
    settings.insert("structures", std::move(structures));

    generator.insert("settings", std::move(settings));
    dim.insert("generator", std::move(generator));

    return dim;
}
