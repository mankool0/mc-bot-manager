#ifndef REGIONFILE_H
#define REGIONFILE_H

#include <QFile>
#include <QString>
#include <tag_compound.h>
#include <array>
#include <cstdint>
#include <vector>

/**
 * Handles Minecraft region files (.mca format).
 * Each region contains up to 32x32 chunks.
 *
 * Format: 8KB headers (locations + timestamps) + chunk data (4KB aligned)
 */
class RegionFile {
public:
    explicit RegionFile(const QString& filepath);
    ~RegionFile();

    // localX, localZ must be in range [0, 31]
    bool writeChunk(int localX, int localZ, const nbt::tag_compound& chunkNBT);
    nbt::tag_compound readChunk(int localX, int localZ);

    bool isValid() const { return file.isOpen(); }
    void flush();

private:
    QFile file;
    QString filepath;

    // Headers (loaded into memory for fast access)
    std::array<uint32_t, 1024> locations;   // Location table
    std::array<uint32_t, 1024> timestamps;  // Timestamp table

    bool headersLoaded = false;

    bool loadHeaders();
    bool saveHeaders();
    bool initializeNewFile();

    static int getHeaderIndex(int localX, int localZ) {
        return (localZ & 31) * 32 + (localX & 31);
    }

    uint32_t allocateSectors(size_t dataSize);  // Returns offset in 4KB sectors

    static std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> zlibDecompress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> serializeNBT(const nbt::tag_compound& nbt);
    static nbt::tag_compound deserializeNBT(const std::vector<uint8_t>& data);
};

#endif // REGIONFILE_H
