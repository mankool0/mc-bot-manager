#include "RegionFile.h"
#include <QDir>
#include <QDataStream>
#include <QtEndian>
#include <io/stream_writer.h>
#include <io/stream_reader.h>
#include <io/ozlibstream.h>
#include <io/izlibstream.h>
#include <zlib.h>
#include <sstream>
#include <cstring>
#include <ctime>

RegionFile::RegionFile(const QString& filepath) : filepath(filepath) {
    // Create directory if it doesn't exist
    QDir dir = QFileInfo(filepath).dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    file.setFileName(filepath);

    // Open or create the file
    if (file.exists()) {
        if (file.open(QIODevice::ReadWrite)) {
            loadHeaders();
        }
    } else {
        if (file.open(QIODevice::ReadWrite)) {
            initializeNewFile();
        }
    }
}

RegionFile::~RegionFile() {
    flush();
    if (file.isOpen()) {
        file.close();
    }
}

bool RegionFile::loadHeaders() {
    if (!file.isOpen()) {
        return false;
    }

    file.seek(0);

    // Read locations (4KB)
    QByteArray locData = file.read(4096);
    if (locData.size() != 4096) {
        return false;
    }

    // Read timestamps (4KB)
    QByteArray timeData = file.read(4096);
    if (timeData.size() != 4096) {
        return false;
    }

    // Parse locations (big-endian uint32)
    for (int i = 0; i < 1024; i++) {
        locations[i] = qFromBigEndian<uint32_t>(reinterpret_cast<const uint8_t*>(locData.data() + i * 4));
        timestamps[i] = qFromBigEndian<uint32_t>(reinterpret_cast<const uint8_t*>(timeData.data() + i * 4));
    }

    headersLoaded = true;

    // Build the free sector map from the loaded headers
    buildFreeSectorMap();

    return true;
}

bool RegionFile::saveHeaders() {
    if (!file.isOpen()) {
        return false;
    }

    file.seek(0);

    // Write locations
    QByteArray locData(4096, 0);
    for (int i = 0; i < 1024; i++) {
        qToBigEndian<uint32_t>(locations[i], reinterpret_cast<uint8_t*>(locData.data() + i * 4));
    }
    file.write(locData);

    // Write timestamps
    QByteArray timeData(4096, 0);
    for (int i = 0; i < 1024; i++) {
        qToBigEndian<uint32_t>(timestamps[i], reinterpret_cast<uint8_t*>(timeData.data() + i * 4));
    }
    file.write(timeData);

    return true;
}

bool RegionFile::initializeNewFile() {
    // Initialize with zero headers
    locations.fill(0);
    timestamps.fill(0);

    headersLoaded = true;

    // Build initial free sector map (only headers are used)
    buildFreeSectorMap();

    return saveHeaders();
}

void RegionFile::buildFreeSectorMap() {
    if (!file.isOpen()) {
        return;
    }

    // Calculate number of sectors in the file
    qint64 fileSize = file.size();
    size_t numSectors = static_cast<size_t>((fileSize + 4095) / 4096);

    // Ensure we have at least 2 sectors for headers
    if (numSectors < 2) {
        numSectors = 2;
    }

    // Initialize all sectors as free
    sectorFree.assign(numSectors, true);

    // Mark first 2 sectors (8KB headers) as used
    sectorFree[0] = false;
    sectorFree[1] = false;

    // Scan all chunk locations and mark their sectors as used
    for (int i = 0; i < 1024; i++) {
        uint32_t location = locations[i];
        if (location == 0) {
            continue;  // Chunk doesn't exist
        }

        uint32_t offset = (location >> 8) & 0xFFFFFF;
        uint32_t sectorCount = location & 0xFF;

        if (offset < 2 || sectorCount == 0) {
            continue;  // Invalid location
        }

        // Mark these sectors as used
        for (uint32_t j = 0; j < sectorCount; j++) {
            uint32_t sector = offset + j;
            if (sector < sectorFree.size()) {
                sectorFree[sector] = false;
            }
        }
    }
}

uint32_t RegionFile::findFreeSectors(size_t count) {
    if (count == 0 || sectorFree.empty()) {
        return 0;
    }

    // Search for a contiguous run of free sectors
    size_t runStart = 0;
    size_t runLength = 0;

    for (size_t i = 0; i < sectorFree.size(); i++) {
        if (sectorFree[i]) {
            // Found a free sector
            if (runLength == 0) {
                runStart = i;
            }
            runLength++;

            if (runLength >= count) {
                // Found enough contiguous free sectors
                return static_cast<uint32_t>(runStart);
            }
        } else {
            // Used sector, reset the run
            runLength = 0;
        }
    }

    // No suitable run found
    return 0;
}

void RegionFile::markSectorsUsed(uint32_t offset, size_t count) {
    // Expand the vector if necessary
    size_t requiredSize = offset + count;
    if (requiredSize > sectorFree.size()) {
        sectorFree.resize(requiredSize, true);
    }

    // Mark sectors as used
    for (size_t i = 0; i < count; i++) {
        sectorFree[offset + i] = false;
    }
}

void RegionFile::markSectorsFree(uint32_t offset, size_t count) {
    // Mark sectors as free (don't expand the vector)
    for (size_t i = 0; i < count; i++) {
        size_t sector = offset + i;
        if (sector < sectorFree.size()) {
            sectorFree[sector] = true;
        }
    }
}

bool RegionFile::writeChunk(int localX, int localZ, const nbt::tag_compound& chunkNBT) {
    if (!isValid() || !headersLoaded) {
        return false;
    }

    // Check if chunk already exists and free old sectors
    int index = getHeaderIndex(localX, localZ);
    uint32_t oldLocation = locations[index];

    if (oldLocation != 0) {
        uint32_t oldOffset = (oldLocation >> 8) & 0xFFFFFF;
        uint32_t oldSectorCount = oldLocation & 0xFF;

        if (oldOffset >= 2 && oldSectorCount > 0) {
            // Free the old sectors
            markSectorsFree(oldOffset, oldSectorCount);
        }
    }

    // Serialize NBT to bytes
    std::vector<uint8_t> nbtData = serializeNBT(chunkNBT);

    // Compress with zlib
    std::vector<uint8_t> compressed = zlibCompress(nbtData);

    // Prepare chunk data with header:
    // 4 bytes: length (excluding this field)
    // 1 byte: compression type (2 = zlib)
    // N bytes: compressed data
    std::vector<uint8_t> chunkData;
    uint32_t length = compressed.size() + 1;  // +1 for compression type byte

    chunkData.resize(5 + compressed.size());
    qToBigEndian<uint32_t>(length, chunkData.data());
    chunkData[4] = 2;  // Zlib compression
    std::memcpy(chunkData.data() + 5, compressed.data(), compressed.size());

    // Calculate sectors needed (round up to 4KB)
    size_t totalSize = chunkData.size();
    size_t sectorsNeeded = (totalSize + 4095) / 4096;

    // Allocate sectors
    uint32_t offset = allocateSectors(totalSize);
    if (offset == 0) {
        return false;
    }

    // Write chunk data
    file.seek(offset * 4096LL);
    file.write(reinterpret_cast<const char*>(chunkData.data()), chunkData.size());

    // Pad to sector boundary
    size_t padding = (sectorsNeeded * 4096) - chunkData.size();
    if (padding > 0) {
        QByteArray paddingData(padding, 0);
        file.write(paddingData);
    }

    // Update header
    locations[index] = (offset << 8) | (sectorsNeeded & 0xFF);
    timestamps[index] = static_cast<uint32_t>(std::time(nullptr));

    return saveHeaders();
}

nbt::tag_compound RegionFile::readChunk(int localX, int localZ) {
    if (!isValid() || !headersLoaded) {
        return nbt::tag_compound();
    }

    int index = getHeaderIndex(localX, localZ);
    uint32_t location = locations[index];

    if (location == 0) {
        // Chunk doesn't exist
        return nbt::tag_compound();
    }

    uint32_t offset = (location >> 8) & 0xFFFFFF;
    uint32_t sectors = location & 0xFF;

    if (offset < 2 || sectors == 0) {
        return nbt::tag_compound();
    }

    // Read chunk data
    file.seek(offset * 4096LL);

    QByteArray lengthBytes = file.read(4);
    if (lengthBytes.size() != 4) {
        return nbt::tag_compound();
    }

    uint32_t length = qFromBigEndian<uint32_t>(reinterpret_cast<const uint8_t*>(lengthBytes.data()));

    QByteArray compressionByte = file.read(1);
    if (compressionByte.size() != 1) {
        return nbt::tag_compound();
    }

    uint8_t compression = static_cast<uint8_t>(compressionByte[0]);

    QByteArray compressedData = file.read(length - 1);
    if (compressedData.size() != static_cast<int>(length - 1)) {
        return nbt::tag_compound();
    }

    // Decompress
    std::vector<uint8_t> compressed(compressedData.begin(), compressedData.end());
    std::vector<uint8_t> decompressed;

    if (compression == 2) {
        // Zlib
        decompressed = zlibDecompress(compressed);
    } else {
        // Unsupported compression
        return nbt::tag_compound();
    }

    // Deserialize NBT
    return deserializeNBT(decompressed);
}

void RegionFile::flush() {
    if (file.isOpen()) {
        file.flush();
    }
}

uint32_t RegionFile::allocateSectors(size_t dataSize) {
    // Calculate sectors needed (round up to 4KB)
    size_t sectorsNeeded = (dataSize + 4095) / 4096;

    if (sectorsNeeded == 0) {
        return 0;
    }

    // Try to find free sectors in existing file
    uint32_t offset = findFreeSectors(sectorsNeeded);

    if (offset != 0) {
        // Found free sectors, mark them as used
        markSectorsUsed(offset, sectorsNeeded);
        return offset;
    }

    // No free sectors found, append to end of file
    qint64 fileSize = file.size();
    offset = static_cast<uint32_t>((fileSize + 4095) / 4096);

    // Ensure offset is at least 2 (after headers)
    if (offset < 2) {
        offset = 2;
    }

    // Mark new sectors as used (this will expand the bitmap)
    markSectorsUsed(offset, sectorsNeeded);

    return offset;
}

std::vector<uint8_t> RegionFile::zlibCompress(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> compressed;

    // Estimate compressed size
    uLongf compressedSize = compressBound(data.size());
    compressed.resize(compressedSize);

    int result = compress(compressed.data(), &compressedSize,
                         data.data(), data.size());

    if (result != Z_OK) {
        return {};
    }

    compressed.resize(compressedSize);
    return compressed;
}

std::vector<uint8_t> RegionFile::zlibDecompress(const std::vector<uint8_t>& data) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = data.size();
    stream.next_in = const_cast<uint8_t*>(data.data());

    if (inflateInit(&stream) != Z_OK) {
        return {};
    }

    std::vector<uint8_t> decompressed;
    const size_t CHUNK_SIZE = 256 * 1024; // 256KB chunks

    int result;
    do {
        size_t oldSize = decompressed.size();
        decompressed.resize(oldSize + CHUNK_SIZE);

        stream.avail_out = CHUNK_SIZE;
        stream.next_out = decompressed.data() + oldSize;

        result = inflate(&stream, Z_NO_FLUSH);

        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }

        decompressed.resize(stream.total_out);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return decompressed;
}

std::vector<uint8_t> RegionFile::serializeNBT(const nbt::tag_compound& nbt) {
    std::ostringstream oss(std::ios::binary);
    nbt::io::write_tag("", nbt, oss);

    std::string str = oss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

nbt::tag_compound RegionFile::deserializeNBT(const std::vector<uint8_t>& data) {
    std::istringstream iss(std::string(data.begin(), data.end()), std::ios::binary);

    try {
        auto pair = nbt::io::read_compound(iss);
        return std::move(*pair.second);
    } catch (...) {
        return nbt::tag_compound();
    }
}
