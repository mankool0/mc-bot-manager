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

// Converts Minecraft world data to NBT format for MCA files and dat files.
class NBTSerializer {
public:
    // Chunk NBT (block data + block entities)
    static nbt::tag_compound chunkToNBT(const ChunkData& chunk, int dataVersion,
                                        const QVector<BlockEntityData>& blockEntities = {});
    static nbt::tag_compound sectionToNBT(const ChunkSection& section);
    static nbt::tag_compound createHeightmaps(const ChunkData& chunk);

    // Example: "minecraft:chest[facing=north,type=single]" -> {Name: "minecraft:chest", Properties: {facing: "north", type: "single"}}
    static nbt::tag_compound blockStateToNBT(const QString& blockState);

    // Block entity NBT
    static nbt::tag_compound blockEntityToNBT(const BlockEntityData& be);
    static nbt::tag_compound itemStackToNBT(const mankool::mcbot::protocol::ItemStack& item);

    // Entity region NBT (entities/ subdirectory)
    static nbt::tag_compound entitiesToNBT(int chunkX, int chunkZ, const QVector<EntityData>& entities,
                                           int dataVersion, bool saveItemEntities = true);
    static nbt::tag_compound entityToNBT(const EntityData& e);

    // Player data NBT ({worldPath}/playerdata/{uuid}.dat)
    static nbt::tag_compound playerToNBT(const PlayerSaveData& data, int dataVersion);

    // Data version thresholds
    static constexpr int DATA_VERSION_1_21_5 = 4325;  // equipment field added for players

private:
    // Builds {id, count, components} without a Slot field. Used for equipment compound and as base for itemStackToNBT.
    static nbt::tag_compound buildItemNBT(const mankool::mcbot::protocol::ItemStack& item);

    static int findHighestBlock(const ChunkData& chunk, int x, int z);  // x, z: 0-15; returns Y or minY if all air
    static void setPackedValue(std::vector<int64_t>& data, int index, int value, int bitsPerEntry);
    static std::vector<nbt::tag_compound> convertPalette(const QVector<QString>& palette);
};

#endif // NBTSERIALIZER_H
