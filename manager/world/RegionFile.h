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
    // ReadWrite creates the directory and file when missing (the writer's mode). ReadOnly opens
    // only what exists and leaves nothing behind, so a scan of a save cannot litter it with
    // empty regions; isValid() is false when the file is absent.
    enum class Mode { ReadWrite, ReadOnly };

    explicit RegionFile(const QString& filepath, Mode mode = Mode::ReadWrite);
    ~RegionFile();

    // localX, localZ must be in range [0, 31]
    bool writeChunk(int localX, int localZ, const nbt::tag_compound& chunkNBT);
    nbt::tag_compound readChunk(int localX, int localZ);
    // The chunk's NBT bytes, decompressed but not parsed; empty when absent or unreadable.
    std::vector<uint8_t> readChunkRaw(int localX, int localZ);
    bool hasChunk(int localX, int localZ) const;

    bool isValid() const { return file.isOpen(); }
    void flush();

private:
    QFile file;
    QString filepath;
    bool readOnly = false;

    // Headers (loaded into memory for fast access)
    std::array<uint32_t, 1024> locations;   // Location table
    std::array<uint32_t, 1024> timestamps;  // Timestamp table

    bool headersLoaded = false;

    // Sector allocation tracking
    std::vector<bool> sectorFree;  // Bitmap tracking which sectors are free

    bool loadHeaders();
    bool saveHeaders();
    bool initializeNewFile();

    static int getHeaderIndex(int localX, int localZ) {
        return (localZ & 31) * 32 + (localX & 31);
    }

    // Sector allocation methods
    void buildFreeSectorMap();
    uint32_t findFreeSectors(size_t count);
    void markSectorsUsed(uint32_t offset, size_t count);
    void markSectorsFree(uint32_t offset, size_t count);
    uint32_t allocateSectors(size_t dataSize);  // Returns offset in 4KB sectors

    static std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> zlibDecompress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> serializeNBT(const nbt::tag_compound& nbt);
    static nbt::tag_compound deserializeNBT(const std::vector<uint8_t>& data);
};

#endif // REGIONFILE_H
