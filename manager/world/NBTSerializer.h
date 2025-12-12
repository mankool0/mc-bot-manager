#ifndef NBTSERIALIZER_H
#define NBTSERIALIZER_H

#include <QString>
#include <QVector>
#include "../bot/WorldData.h"
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_array.h>
#include <tag_primitive.h>
#include <tag_string.h>
#include <cstdint>
#include <vector>

// Converts ChunkData to Minecraft NBT format for MCA files.
class NBTSerializer {
public:
    static nbt::tag_compound chunkToNBT(const ChunkData& chunk, int dataVersion);
    static nbt::tag_compound sectionToNBT(const ChunkSection& section);
    static nbt::tag_compound createHeightmaps(const ChunkData& chunk);

    // Example: "minecraft:chest[facing=north,type=single]" -> {Name: "minecraft:chest", Properties: {facing: "north", type: "single"}}
    static nbt::tag_compound blockStateToNBT(const QString& blockState);

private:
    static int findHighestBlock(const ChunkData& chunk, int x, int z);  // x, z: 0-15; returns Y or minY if all air
    static void setPackedValue(std::vector<int64_t>& data, int index, int value, int bitsPerEntry);
    static std::vector<nbt::tag_compound> convertPalette(const QVector<QString>& palette);
};

#endif // NBTSERIALIZER_H
