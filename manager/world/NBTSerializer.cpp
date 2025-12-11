#include "NBTSerializer.h"
#include <QRegularExpression>
#include <QStringList>
#include <ctime>
#include <algorithm>

nbt::tag_compound NBTSerializer::chunkToNBT(const ChunkData& chunk, int dataVersion) {
    nbt::tag_compound root;

    root.insert("DataVersion", nbt::tag_int(dataVersion));
    root.insert("xPos", nbt::tag_int(chunk.chunkX));
    root.insert("zPos", nbt::tag_int(chunk.chunkZ));

    // Calculate yPos (lowest section Y index)
    int lowestSectionY = 0;
    if (!chunk.sections.isEmpty()) {
        lowestSectionY = chunk.sections.firstKey();
    }
    root.insert("yPos", nbt::tag_int(lowestSectionY));

    root.insert("Status", nbt::tag_string("minecraft:full"));
    root.insert("LastUpdate", nbt::tag_long(std::time(nullptr)));
    root.insert("InhabitedTime", nbt::tag_long(0));

    // Convert sections
    nbt::tag_list sections(nbt::tag_type::Compound);
    for (auto it = chunk.sections.constBegin(); it != chunk.sections.constEnd(); ++it) {
        sections.push_back(sectionToNBT(it.value()));
    }
    root.insert("sections", std::move(sections));

    // Heightmaps (required for proper rendering)
    root.insert("Heightmaps", createHeightmaps(chunk));

    // Empty lists for optional data
    root.insert("block_entities", nbt::tag_list(nbt::tag_type::Compound));
    root.insert("PostProcessing", nbt::tag_list(nbt::tag_type::List));

    return root;
}

nbt::tag_compound NBTSerializer::sectionToNBT(const ChunkSection& section) {
    nbt::tag_compound sectionTag;

    sectionTag.insert("Y", nbt::tag_byte(section.sectionY));

    nbt::tag_compound blockStates;

    // Convert palette
    std::vector<nbt::tag_compound> paletteTags = convertPalette(section.palette);
    nbt::tag_list paletteList(nbt::tag_type::Compound);
    for (auto& paletteTag : paletteTags) {
        paletteList.push_back(std::move(paletteTag));
    }
    blockStates.insert("palette", std::move(paletteList));

    // Convert indices if not uniform
    if (!section.uniform && !section.blockIndices.isEmpty()) {
        // Pack indices into long array using Minecraft's variable-width format
        // Uses 4-8 bits per entry for indirect palette, or 15 bits for direct palette
        int bitsPerEntry = std::max(4, static_cast<int>(std::ceil(std::log2(section.palette.size()))));
        if (bitsPerEntry > 8) {
            bitsPerEntry = 15;  // Direct palette
        }

        int entriesPerLong = 64 / bitsPerEntry;
        int longCount = (4096 + entriesPerLong - 1) / entriesPerLong;

        std::vector<int64_t> packedData(longCount, 0);

        for (int i = 0; i < section.blockIndices.size() && i < 4096; i++) {
            int longIndex = i / entriesPerLong;
            int bitOffset = (i % entriesPerLong) * bitsPerEntry;
            uint32_t value = section.blockIndices[i];
            uint64_t mask = ((1ULL << bitsPerEntry) - 1);
            packedData[longIndex] |= (static_cast<uint64_t>(value) & mask) << bitOffset;
        }

        blockStates.insert("data", nbt::tag_long_array(std::move(packedData)));
    }

    sectionTag.insert("block_states", std::move(blockStates));

    // Simple biomes (all plains for now)
    nbt::tag_compound biomes;
    nbt::tag_list biomePalette(nbt::tag_type::String);
    biomePalette.push_back(nbt::tag_string("minecraft:plains"));
    biomes.insert("palette", std::move(biomePalette));
    sectionTag.insert("biomes", std::move(biomes));

    return sectionTag;
}

nbt::tag_compound NBTSerializer::createHeightmaps(const ChunkData& chunk) {
    nbt::tag_compound maps;

    // MOTION_BLOCKING: Highest non-air block
    // 256 values (16x16) packed into longs with 9 bits each
    int bitsPerEntry = 9;  // Can store heights up to 512
    int entriesPerLong = 64 / bitsPerEntry;  // 7 entries per long
    int longCount = (256 + entriesPerLong - 1) / entriesPerLong;  // 37 longs

    std::vector<int64_t> motionBlocking(longCount, 0);

    for (int z = 0; z < 16; z++) {
        for (int x = 0; x < 16; x++) {
            int height = findHighestBlock(chunk, x, z);
            int index = z * 16 + x;
            setPackedValue(motionBlocking, index, height, bitsPerEntry);
        }
    }

    maps.insert("MOTION_BLOCKING", nbt::tag_long_array(std::move(motionBlocking)));

    return maps;
}

nbt::tag_compound NBTSerializer::blockStateToNBT(const QString& blockState) {
    nbt::tag_compound tag;

    // Parse format: "minecraft:block[property=value,...]"
    int bracketPos = blockState.indexOf('[');

    QString blockName;
    if (bracketPos == -1) {
        // No properties
        blockName = blockState;
    } else {
        blockName = blockState.left(bracketPos);

        // Parse properties
        QString propertiesStr = blockState.mid(bracketPos + 1);
        propertiesStr = propertiesStr.left(propertiesStr.length() - 1);  // Remove trailing ]

        QStringList properties = propertiesStr.split(',');
        if (!properties.isEmpty() && !properties[0].isEmpty()) {
            nbt::tag_compound props;
            for (const QString& prop : properties) {
                QStringList parts = prop.split('=');
                if (parts.size() == 2) {
                    props.insert(parts[0].toStdString(), nbt::tag_string(parts[1].toStdString()));
                }
            }
            tag.insert("Properties", std::move(props));
        }
    }

    tag.insert("Name", nbt::tag_string(blockName.toStdString()));

    return tag;
}

int NBTSerializer::findHighestBlock(const ChunkData& chunk, int x, int z) {
    // Search from top to bottom for first non-air block
    for (int y = chunk.maxY - 1; y >= chunk.minY; y--) {
        auto block = chunk.getBlock(x, y, z);
        if (block.has_value() && !block->contains("air")) {
            return y - chunk.minY;  // Return relative height
        }
    }
    return 0;  // All air
}

void NBTSerializer::setPackedValue(std::vector<int64_t>& data, int index, int value, int bitsPerEntry) {
    int entriesPerLong = 64 / bitsPerEntry;
    int longIndex = index / entriesPerLong;
    int bitOffset = (index % entriesPerLong) * bitsPerEntry;

    if (longIndex >= static_cast<int>(data.size())) {
        return;  // Out of bounds
    }

    uint64_t mask = (1ULL << bitsPerEntry) - 1;
    data[longIndex] |= (static_cast<uint64_t>(value) & mask) << bitOffset;
}

std::vector<nbt::tag_compound> NBTSerializer::convertPalette(const QVector<QString>& palette) {
    std::vector<nbt::tag_compound> result;
    result.reserve(palette.size());

    for (const QString& blockState : palette) {
        result.push_back(blockStateToNBT(blockState));
    }

    return result;
}
