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
    return saveHeaders();
}

bool RegionFile::writeChunk(int localX, int localZ, const nbt::tag_compound& chunkNBT) {
    if (!isValid() || !headersLoaded) {
        return false;
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
    int index = getHeaderIndex(localX, localZ);
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
    // Simple allocation: always append to end of file
    // In a real implementation, we'd track free sectors and reuse them
    (void)dataSize;  // Currently unused

    qint64 fileSize = file.size();
    uint32_t offset = static_cast<uint32_t>((fileSize + 4095) / 4096);

    // Ensure offset is at least 2 (after headers)
    if (offset < 2) {
        offset = 2;
    }

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
    std::vector<uint8_t> decompressed;

    // Try decompressing with increasingly larger buffers
    for (size_t bufferSize = data.size() * 4; bufferSize < data.size() * 1024; bufferSize *= 2) {
        decompressed.resize(bufferSize);
        uLongf decompressedSize = bufferSize;

        int result = uncompress(decompressed.data(), &decompressedSize,
                              data.data(), data.size());

        if (result == Z_OK) {
            decompressed.resize(decompressedSize);
            return decompressed;
        }

        if (result != Z_BUF_ERROR) {
            return {};
        }
    }

    return {};
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
