#ifndef SECTIONCODEC_H
#define SECTIONCODEC_H

#include <QByteArray>
#include <QVector>
#include <optional>

struct ChunkSection;

// Canonical encoding, content digest and bulk export framing of 16x16x16 chunk
// sections.
//
// This layer is format-neutral: it turns a ChunkSection into a form where
// identical terrain encodes identically however the in-memory palette grew,
// hashes that form under a caller-supplied domain prefix, and frames batches
// of encoded sections for export.
//
// Consumers pick their own prefix (Python: the digest_prefix parameter of
// world.changed_sections / world.get_section). It keeps this format's digests
// apart from any other blob format sharing a content-addressed store, and it
// is the version knob: a consumer bumps its tag when what it stores changes,
// so stale digests stop matching instead of silently colliding.
//
// Canonical form: the palette is exactly the set of blockstate strings the
// indices reference (bloat from incremental setBlock calls compacted away),
// sorted bytewise as UTF-8 and deduplicated; indices are 4096 u16 values in YZX
// order (index = y*256 + z*16 + x). Identical terrain must digest identically
// however the palettes grew, or a digest-compare sync breaks and every section
// uploads forever.
//
// The encoding is a cross-language contract. Implementations outside this repo
// depend on it byte for byte, so treat it as frozen. Known-answer vectors,
// digested under the 9-byte prefix "hiveobs1\0" (NUL included):
//
//   uniform minecraft:air, all 4096 indices 0
//     6e486e74b975a44889dcbbcafb07a99d84a3f6fbb72559f298eed6d9e863c05e
//   input palette [minecraft:netherrack, minecraft:obsidian, minecraft:air]
//   with input index i = i % 3, canonicalized to palette
//   [minecraft:air, minecraft:netherrack, minecraft:obsidian] and indices
//   1, 2, 0 repeating
//     52c80fddbcfa027680bef260300d064f9740a798f0db3fe1e10404abf32df991
//
// Light, biomes and block entities are excluded from the blob, the digest and
// the export framing.
namespace SectionCodec {

struct CanonicalSection {
    QVector<QByteArray> palette;
    QVector<quint16> indices;
};

// Returns std::nullopt for a malformed section (non-uniform without a full
// 4096-entry index array). Resolution mirrors ChunkSection::getBlock: a
// uniform section with an empty palette and an out-of-range palette index both
// read as minecraft:air.
std::optional<CanonicalSection> canonicalize(const ChunkSection &section);

// u32le(palette_len) || [u16le(name_len) || utf8_name]* || u16le(index)*4096
QByteArray encodeBlob(const CanonicalSection &section);

// BLAKE2b-256(prefix || blob). Returns 32 bytes. The digest covers content and
// nothing else, so identical terrain shares a digest wherever it occurs - which
// is what lets a content-addressed store hold one copy. `prefix` is the
// caller's format/domain tag; an empty prefix hashes the bare blob.
QByteArray digest(const CanonicalSection &section, QByteArrayView prefix);

struct SectionFrame {
    QByteArray dimensionUtf8;
    qint32 chunkX = 0;
    qint32 chunkZ = 0;
    qint32 sectionY = 0;
    QByteArray blob;
};

// The manager's bulk section export framing (world.export_sections):
// u32le(n) || [u16le(dim_len) || dim || i32le(cx) || i32le(cz) || i32le(sy) ||
// u32le(blob_len) || blob]*. Uncompressed. Each frame carries its section's
// location beside the canonical blob, so a content-addressed receiver can store
// the blob by digest and record the location separately.
QByteArray encodeExport(const QVector<SectionFrame> &frames);

}

#endif // SECTIONCODEC_H
