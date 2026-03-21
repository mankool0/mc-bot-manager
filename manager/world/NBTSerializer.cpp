#include "NBTSerializer.h"
#include <io/stream_reader.h>
#include <QRegularExpression>
#include <QStringList>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cmath>

nbt::tag_compound NBTSerializer::chunkToNBT(const ChunkData& chunk, int dataVersion,
                                             const QVector<BlockEntityData>& blockEntities) {
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

    // Block entities
    nbt::tag_list blockEntitiesTag(nbt::tag_type::Compound);
    for (const auto& be : blockEntities) {
        blockEntitiesTag.push_back(blockEntityToNBT(be));
    }
    root.insert("block_entities", std::move(blockEntitiesTag));
    root.insert("PostProcessing", nbt::tag_list(nbt::tag_type::List));

    return root;
}

nbt::tag_compound NBTSerializer::buildItemNBT(const mankool::mcbot::protocol::ItemStack& item) {
    // Java client sends the full compound payload from ItemStack.CODEC: {id, count, components}.
    // Slot is not included - callers add it as appropriate.
    const QByteArray nbtBytes = item.nbt();
    if (!nbtBytes.isEmpty()) {
        try {
            std::istringstream ss(std::string(nbtBytes.data(), nbtBytes.size()), std::ios::binary);
            nbt::io::stream_reader reader(ss);
            auto tagPtr = reader.read_payload(nbt::tag_type::Compound);
            return std::move(static_cast<nbt::tag_compound&>(*tagPtr));
        } catch (const std::exception&) {}
    }

    return nbt::tag_compound{};
}

nbt::tag_compound NBTSerializer::itemStackToNBT(const mankool::mcbot::protocol::ItemStack& item) {
    nbt::tag_compound tag = buildItemNBT(item);
    tag.insert("Slot", nbt::tag_byte(static_cast<int8_t>(item.slot())));
    return tag;
}

nbt::tag_compound NBTSerializer::blockEntityToNBT(const BlockEntityData& be) {
    // If raw binary NBT is available (from chunk load), use it as the base.
    // If items are also known (container was opened this session), patch them in
    // so we keep all original fields (lock, custom name, furnace progress, etc.)
    // while writing the freshest item contents.
    if (!be.rawNbt.isEmpty()) {
        try {
            std::istringstream ss(std::string(be.rawNbt.constData(), be.rawNbt.size()), std::ios::binary);
            nbt::io::stream_reader reader(ss);
            auto tagPtr = reader.read_payload(nbt::tag_type::Compound);
            auto& compound = static_cast<nbt::tag_compound&>(*tagPtr);

            if (!be.items.isEmpty()) {
                nbt::tag_list items(nbt::tag_type::Compound);
                for (const auto& item : be.items) {
                    if (!item.itemId().isEmpty() && item.itemId() != "minecraft:air") {
                        items.push_back(itemStackToNBT(item));
                    }
                }
                compound["Items"] = nbt::value(std::move(items));
            }

            return std::move(compound);
        } catch (...) {}
    }

    // rawNbt should always be present (set from chunk load and preserved through container opens).
    // Return an empty compound as a last-resort fallback so callers always get a valid tag.
    return nbt::tag_compound{};
}

nbt::tag_compound NBTSerializer::entityToNBT(const EntityData& e) {
    nbt::tag_compound tag;

    tag.insert("id", nbt::tag_string(e.type.toStdString()));

    // UUID as [I; a, b, c, d] (4 big-endian int32s from hex string)
    QString uuidStr = e.uuid;
    uuidStr.remove('-');
    if (uuidStr.length() == 32) {
        std::vector<int32_t> uuidInts;
        for (int i = 0; i < 4; i++) {
            bool ok;
            uint32_t val = uuidStr.mid(i * 8, 8).toUInt(&ok, 16);
            uuidInts.push_back(static_cast<int32_t>(val));
        }
        tag.insert("UUID", nbt::tag_int_array(std::move(uuidInts)));
    }

    // Position
    nbt::tag_list pos(nbt::tag_type::Double);
    pos.push_back(nbt::tag_double(e.x));
    pos.push_back(nbt::tag_double(e.y));
    pos.push_back(nbt::tag_double(e.z));
    tag.insert("Pos", std::move(pos));

    // Rotation
    nbt::tag_list rot(nbt::tag_type::Float);
    rot.push_back(nbt::tag_float(e.yaw));
    rot.push_back(nbt::tag_float(e.pitch));
    tag.insert("Rotation", std::move(rot));

    // Motion (velocity)
    nbt::tag_list motion(nbt::tag_type::Double);
    motion.push_back(nbt::tag_double(e.velX));
    motion.push_back(nbt::tag_double(e.velY));
    motion.push_back(nbt::tag_double(e.velZ));
    tag.insert("Motion", std::move(motion));

    tag.insert("OnGround", nbt::tag_byte(0));

    if (e.isLiving) {
        tag.insert("Health", nbt::tag_float(e.health));
    }

    if (e.isItem) {
        nbt::tag_compound itemTag;
        itemTag.insert("id",    nbt::tag_string(e.itemStack.itemId().toStdString()));
        itemTag.insert("count", nbt::tag_byte(static_cast<int8_t>(e.itemStack.count())));
        tag.insert("Item", std::move(itemTag));
    }

    return tag;
}

nbt::tag_compound NBTSerializer::entitiesToNBT(int chunkX, int chunkZ, const QVector<EntityData>& entities,
                                                int dataVersion, bool saveItemEntities) {
    nbt::tag_compound root;
    root.insert("DataVersion", nbt::tag_int(dataVersion));

    // Position as [I; chunkX, chunkZ]
    std::vector<int32_t> posVec = {chunkX, chunkZ};
    root.insert("Position", nbt::tag_int_array(std::move(posVec)));

    nbt::tag_list entityList(nbt::tag_type::Compound);
    for (const auto& e : entities) {
        if (e.isPlayer) continue;
        if (!saveItemEntities && e.isItem) continue;
        entityList.push_back(entityToNBT(e));
    }
    root.insert("Entities", std::move(entityList));

    return root;
}

nbt::tag_compound NBTSerializer::playerToNBT(const PlayerSaveData& data, int dataVersion) {
    nbt::tag_compound root;

    root.insert("DataVersion",    nbt::tag_int(dataVersion));
    root.insert("playerGameType", nbt::tag_int(0));  // survival

    // Position
    nbt::tag_list pos(nbt::tag_type::Double);
    pos.push_back(nbt::tag_double(data.x));
    pos.push_back(nbt::tag_double(data.y));
    pos.push_back(nbt::tag_double(data.z));
    root.insert("Pos", std::move(pos));

    // Rotation
    nbt::tag_list rot(nbt::tag_type::Float);
    rot.push_back(nbt::tag_float(data.yaw));
    rot.push_back(nbt::tag_float(data.pitch));
    root.insert("Rotation", std::move(rot));

    root.insert("Dimension",          nbt::tag_string(data.dimension.toStdString()));
    root.insert("Health",             nbt::tag_float(data.health));
    root.insert("foodLevel",          nbt::tag_int(data.foodLevel));
    root.insert("foodSaturationLevel",nbt::tag_float(data.saturation));
    root.insert("XpLevel",            nbt::tag_int(data.experienceLevel));
    root.insert("XpP",                nbt::tag_float(data.experienceProgress));
    root.insert("XpTotal",            nbt::tag_int(data.totalExperience));
    root.insert("Score",              nbt::tag_int(0));  // Cross-death score, not tracked by bot

    // Inventory (slots 0-35) and equipment (armor slots 36-39, offhand 40).
    // Proto slots 36-39 = armor (boots/leggings/chestplate/helmet), 40 = offhand.
    // 1.21.5+ (dataVersion >= DATA_VERSION_1_21_5): store in equipment compound.
    // Older: store in Inventory list with legacy slot IDs 100-103 / -106.
    nbt::tag_list inventory(nbt::tag_type::Compound);
    nbt::tag_compound equipment;
    bool hasEquipment = false;

    for (const auto& item : data.inventory) {
        if (item.itemId().isEmpty() || item.itemId() == "minecraft:air") continue;

        int protoSlot = item.slot();

        if (protoSlot >= 36 && protoSlot <= 40) {
            if (dataVersion >= DATA_VERSION_1_21_5) {
                // New format: equipment compound with named slots, no Slot field on item
                const char* slotName = nullptr;
                switch (protoSlot) {
                    case 36: slotName = "feet";    break;
                    case 37: slotName = "legs";    break;
                    case 38: slotName = "chest";   break;
                    case 39: slotName = "head";    break;
                    case 40: slotName = "offhand"; break;
                }
                if (slotName) {
                    equipment.insert(slotName, buildItemNBT(item));
                    hasEquipment = true;
                }
            } else {
                // Legacy format: Inventory list with slot IDs 100-103 / -106
                int8_t datSlot;
                switch (protoSlot) {
                    case 36: datSlot = 100; break;
                    case 37: datSlot = 101; break;
                    case 38: datSlot = 102; break;
                    case 39: datSlot = 103; break;
                    case 40: datSlot = static_cast<int8_t>(-106); break;
                    default: datSlot = static_cast<int8_t>(protoSlot); break;
                }
                nbt::tag_compound itemTag = buildItemNBT(item);
                itemTag.insert("Slot", nbt::tag_byte(datSlot));
                inventory.push_back(std::move(itemTag));
            }
        } else {
            inventory.push_back(itemStackToNBT(item));
        }
    }

    root.insert("Inventory", std::move(inventory));
    if (hasEquipment) {
        root.insert("equipment", std::move(equipment));
    }

    // Ender chest contents (only written when known; processPlayerData injects from disk otherwise)
    if (!data.enderItems.isEmpty()) {
        nbt::tag_list enderItems(nbt::tag_type::Compound);
        for (const auto& item : data.enderItems) {
            if (!item.itemId().isEmpty() && item.itemId() != "minecraft:air") {
                enderItems.push_back(itemStackToNBT(item));
            }
        }
        root.insert("EnderItems", std::move(enderItems));
    }

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

    // Biomes
    nbt::tag_compound biomes;
    nbt::tag_list biomePaletteTag(nbt::tag_type::String);

    if (section.biomePalette.isEmpty()) {
        biomePaletteTag.push_back(nbt::tag_string("minecraft:the_void"));
    } else {
        for (const QString& biomeId : section.biomePalette) {
            biomePaletteTag.push_back(nbt::tag_string(biomeId.toStdString()));
        }

        if (section.biomePalette.size() > 1 && !section.biomeIndices.isEmpty()) {
            int bitsPerEntry = static_cast<int>(std::ceil(std::log2(section.biomePalette.size())));
            int entriesPerLong = 64 / bitsPerEntry;
            int longCount = (64 + entriesPerLong - 1) / entriesPerLong;

            std::vector<int64_t> packedData(longCount, 0);
            for (int i = 0; i < section.biomeIndices.size() && i < 64; i++) {
                int longIndex = i / entriesPerLong;
                int bitOffset = (i % entriesPerLong) * bitsPerEntry;
                uint64_t mask = (1ULL << bitsPerEntry) - 1;
                packedData[longIndex] |= (static_cast<uint64_t>(section.biomeIndices[i]) & mask) << bitOffset;
            }
            biomes.insert("data", nbt::tag_long_array(std::move(packedData)));
        }
    }

    biomes.insert("palette", std::move(biomePaletteTag));
    sectionTag.insert("biomes", std::move(biomes));

    // Light data
    if (section.blockLight.size() == 2048) {
        std::vector<int8_t> blockLightVec(section.blockLight.constData(),
                                          section.blockLight.constData() + 2048);
        sectionTag.insert("BlockLight", nbt::tag_byte_array(std::move(blockLightVec)));
    }
    if (section.skyLight.size() == 2048) {
        std::vector<int8_t> skyLightVec(section.skyLight.constData(),
                                        section.skyLight.constData() + 2048);
        sectionTag.insert("SkyLight", nbt::tag_byte_array(std::move(skyLightVec)));
    }

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

ChunkSection NBTSerializer::nbtToChunkSection(const nbt::tag_compound& section) {
    ChunkSection result;

    if (section.has_key("Y")) {
        try {
            result.sectionY = static_cast<int32_t>(
                static_cast<const nbt::tag_byte&>(section.at("Y").get()).get());
        } catch (...) {}
    }

    // Block states
    if (section.has_key("block_states")) {
        try {
            const auto& blockStates = static_cast<const nbt::tag_compound&>(section.at("block_states").get());

            // Parse palette
            if (blockStates.has_key("palette")) {
                const auto& paletteList = static_cast<const nbt::tag_list&>(blockStates.at("palette").get());
                for (const nbt::value& entry : paletteList) {
                    const auto& entryTag = static_cast<const nbt::tag_compound&>(entry.get());
                    QString blockName;
                    if (entryTag.has_key("Name")) {
                        blockName = QString::fromStdString(
                            static_cast<const nbt::tag_string&>(entryTag.at("Name").get()).get());
                    }
                    if (entryTag.has_key("Properties")) {
                        const auto& props = static_cast<const nbt::tag_compound&>(entryTag.at("Properties").get());
                        QStringList propParts;
                        for (auto it = props.begin(); it != props.end(); ++it) {
                            QString k = QString::fromStdString(it->first);
                            QString v = QString::fromStdString(
                                static_cast<const nbt::tag_string&>(it->second.get()).get());
                            propParts.append(k + "=" + v);
                        }
                        if (!propParts.isEmpty()) {
                            blockName += "[" + propParts.join(",") + "]";
                        }
                    }
                    result.palette.append(blockName);
                }
            }

            // Parse packed block data
            if (blockStates.has_key("data")) {
                const auto& dataArr = static_cast<const nbt::tag_long_array&>(blockStates.at("data").get());
                int paletteSize = result.palette.size();
                int bitsPerEntry = std::max(4, static_cast<int>(std::ceil(std::log2(std::max(paletteSize, 2)))));
                if (bitsPerEntry > 8) bitsPerEntry = 15;
                int entriesPerLong = 64 / bitsPerEntry;
                uint64_t mask = (1ULL << bitsPerEntry) - 1;

                result.blockIndices.resize(4096, 0);
                for (int i = 0; i < 4096; i++) {
                    int longIndex = i / entriesPerLong;
                    int bitOffset = (i % entriesPerLong) * bitsPerEntry;
                    if (longIndex < static_cast<int>(dataArr.size())) {
                        uint64_t longVal = static_cast<uint64_t>(dataArr[longIndex]);
                        result.blockIndices[i] = static_cast<uint32_t>((longVal >> bitOffset) & mask);
                    }
                }
            } else {
                // No data array means single-value section (uniform)
                result.uniform = true;
            }
        } catch (...) {}
    }

    // Biomes
    if (section.has_key("biomes")) {
        try {
            const auto& biomes = static_cast<const nbt::tag_compound&>(section.at("biomes").get());
            if (biomes.has_key("palette")) {
                const auto& biomePaletteList = static_cast<const nbt::tag_list&>(biomes.at("palette").get());
                for (const nbt::value& entry : biomePaletteList) {
                    result.biomePalette.append(QString::fromStdString(
                        static_cast<const nbt::tag_string&>(entry.get()).get()));
                }
            }
            if (biomes.has_key("data") && result.biomePalette.size() > 1) {
                const auto& dataArr = static_cast<const nbt::tag_long_array&>(biomes.at("data").get());
                int bitsPerEntry = static_cast<int>(std::ceil(std::log2(result.biomePalette.size())));
                int entriesPerLong = 64 / bitsPerEntry;
                uint64_t mask = (1ULL << bitsPerEntry) - 1;
                result.biomeIndices.resize(64, 0);
                for (int i = 0; i < 64; i++) {
                    int longIndex = i / entriesPerLong;
                    int bitOffset = (i % entriesPerLong) * bitsPerEntry;
                    if (longIndex < static_cast<int>(dataArr.size())) {
                        uint64_t longVal = static_cast<uint64_t>(dataArr[longIndex]);
                        result.biomeIndices[i] = static_cast<uint32_t>((longVal >> bitOffset) & mask);
                    }
                }
            } else if (result.biomePalette.size() <= 1) {
                result.biomeUniform = true;
            }
        } catch (...) {}
    }

    // Light data
    if (section.has_key("BlockLight")) {
        try {
            const auto& arr = static_cast<const nbt::tag_byte_array&>(section.at("BlockLight").get());
            if (arr.size() == 2048) {
                result.blockLight.resize(2048);
                for (int i = 0; i < 2048; i++) {
                    result.blockLight[i] = static_cast<char>(arr[i]);
                }
            }
        } catch (...) {}
    }
    if (section.has_key("SkyLight")) {
        try {
            const auto& arr = static_cast<const nbt::tag_byte_array&>(section.at("SkyLight").get());
            if (arr.size() == 2048) {
                result.skyLight.resize(2048);
                for (int i = 0; i < 2048; i++) {
                    result.skyLight[i] = static_cast<char>(arr[i]);
                }
            }
        } catch (...) {}
    }

    return result;
}

ChunkData NBTSerializer::nbtToChunk(const nbt::tag_compound& root) {
    ChunkData result;

    try {
        if (root.has_key("xPos")) result.chunkX = static_cast<const nbt::tag_int&>(root.at("xPos").get()).get();
        if (root.has_key("zPos")) result.chunkZ = static_cast<const nbt::tag_int&>(root.at("zPos").get()).get();
    } catch (...) {}

    if (root.has_key("sections")) {
        try {
            const auto& sectionsList = static_cast<const nbt::tag_list&>(root.at("sections").get());
            for (const nbt::value& entry : sectionsList) {
                const auto& sectionTag = static_cast<const nbt::tag_compound&>(entry.get());
                ChunkSection sec = nbtToChunkSection(sectionTag);
                result.sections[sec.sectionY] = std::move(sec);
            }
        } catch (...) {}
    }

    if (!result.sections.isEmpty()) {
        result.minY = result.sections.firstKey() * 16;
        result.maxY = (result.sections.lastKey() + 1) * 16;
    }

    return result;
}

QVector<BlockEntityData> NBTSerializer::nbtToBlockEntities(const nbt::tag_compound& root, const QString& dimension) {
    QVector<BlockEntityData> result;

    if (!root.has_key("block_entities")) return result;

    try {
        const auto& beList = static_cast<const nbt::tag_list&>(root.at("block_entities").get());
        for (const nbt::value& entry : beList) {
            const auto& be = static_cast<const nbt::tag_compound&>(entry.get());
            BlockEntityData data;
            if (be.has_key("id")) {
                data.type = QString::fromStdString(
                    static_cast<const nbt::tag_string&>(be.at("id").get()).get());
            }
            if (be.has_key("x")) data.x = static_cast<const nbt::tag_int&>(be.at("x").get()).get();
            if (be.has_key("y")) data.y = static_cast<const nbt::tag_int&>(be.at("y").get()).get();
            if (be.has_key("z")) data.z = static_cast<const nbt::tag_int&>(be.at("z").get()).get();
            data.dimension = dimension;
            result.append(data);
        }
    } catch (...) {}

    return result;
}
