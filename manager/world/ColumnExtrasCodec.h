#ifndef COLUMNEXTRASCODEC_H
#define COLUMNEXTRASCODEC_H

#include <QByteArray>
#include <QMap>
#include <QVector>
#include <optional>

struct BlockEntityData;
struct ChunkSection;

// Canonical encoding, content digest and bulk export framing of a chunk column's
// "extras": what SectionCodec leaves out of a section blob - the biomes of each of
// the column's sections and the NBT of every block entity in it. One record per
// column, not per section, because that is how a world save stores block entities
// and because most columns carry nothing but one biome per section.
//
// Like SectionCodec this is a cross-language contract (a consumer re-derives and
// checks the digest), so treat the encoding as frozen; a new layout gets a new
// format byte and the consumer a new digest prefix.
//
//   u8 format (1)
//   u8 section_count
//   per section, ascending section_y; a section with no biome data is left out:
//     i8 section_y
//     u8 biome_count                                    1..64
//     [u16le(name_len) || utf8_name] * biome_count      referenced only, sorted bytewise
//     u8 index * 64, only when biome_count > 1          index = y*16 + z*4 + x
//   u32le block_entity_count
//   per block entity, sorted by (y, z, x):
//     i32le x || i32le y || i32le z                      absolute, inside the column
//     u8 flags                                           bit 0: contents seen
//     u32le nbt_len || nbt                               compound payload, no root header
//
// The NBT is the client's saveWithFullMetadata compound, re-serialized through
// libnbt++ so its keys come out sorted bytewise at every level (the client writes
// them in Java HashMap order, which is not stable), with "Items" replaced by what a
// bot saw in it when it opened it. "Contents seen" says the Items are the
// container's whole contents, an empty list included; without it an Items list is
// whatever the server put in the client's copy, which for a container is nothing.
//
// Known-answer vector, digested under the 9-byte prefix "hiveext1\0" (NUL
// included): section 0 uniform minecraft:nether_wastes; section 1 with input
// palette [minecraft:soul_sand_valley, minecraft:basalt_deltas] and input index
// i % 2; one block entity at (1, 20, 2), not opened, NBT
// {id: "minecraft:sign", x: 1, y: 20, z: 2, is_waxed: 0b} sent in that key order:
//
//   ae0e002e93a457cefea6c7c4ac40560bddc819223bbb7de533ad6939b8e80dfb
namespace ColumnExtrasCodec {

constexpr quint8 kFormat = 1;
constexpr quint8 kContentsSeen = 0x01;
constexpr int kBiomeCells = 64;  // 4x4x4

struct Biomes {
    qint32 sectionY = 0;
    QVector<QByteArray> palette;  // sorted bytewise, referenced entries only
    QVector<quint8> indices;      // 64 entries; empty when the palette has one entry
};

struct BlockEntity {
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    quint8 flags = 0;
    QByteArray nbt;
};

struct CanonicalColumn {
    QVector<Biomes> sections;            // ascending sectionY
    QVector<BlockEntity> blockEntities;  // sorted by (y, z, x)
};

// A section's biomes in canonical form; std::nullopt when it has none (a section made by
// setBlock), an index out of range, or a section_y outside i8.
std::optional<Biomes> canonicalBiomes(const ChunkSection &section);

// The block entity's NBT in canonical form with its seen contents patched in; std::nullopt
// when there is nothing readable to write.
std::optional<QByteArray> canonicalNbt(const BlockEntityData &be);

// Block entities outside the column are dropped rather than trusted.
CanonicalColumn canonicalize(qint32 chunkX, qint32 chunkZ, const QMap<int32_t, ChunkSection> &sections,
                             const QVector<BlockEntityData> &blockEntities);

QByteArray encodeBlob(const CanonicalColumn &column);

// BLAKE2b-256(prefix || blob), 32 bytes.
QByteArray digest(const QByteArray &blob, QByteArrayView prefix);

// world.export_extras framing:
// u32le(n) || [u16le(dim_len) || dim || i32le(cx) || i32le(cz) || u32le(blob_len) || blob]*.
struct ColumnFrame {
    QByteArray dimensionUtf8;
    qint32 chunkX = 0;
    qint32 chunkZ = 0;
    QByteArray blob;
};

QByteArray encodeExport(const QVector<ColumnFrame> &frames);

}

#endif // COLUMNEXTRASCODEC_H
