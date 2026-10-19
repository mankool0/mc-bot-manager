#include "ColumnExtrasCodec.h"
#include "NBTSerializer.h"
#include "SectionCodecPrimitives.h"
#include "bot/WorldData.h"

#include <QCryptographicHash>
#include <QVarLengthArray>
#include <io/stream_writer.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <tuple>

namespace ColumnExtrasCodec {

namespace {

void appendU16le(QByteArray &out, quint16 v)
{
    char b[2];
    putU16le(b, v);
    out.append(b, 2);
}

void appendU32le(QByteArray &out, quint32 v)
{
    char b[4];
    putU32le(b, v);
    out.append(b, 4);
}

}

std::optional<Biomes> canonicalBiomes(const ChunkSection &section)
{
    if (section.sectionY < -128 || section.sectionY > 127 || section.biomePalette.isEmpty()) {
        return std::nullopt;
    }
    Biomes out;
    out.sectionY = section.sectionY;
    if (section.biomeUniform) {
        out.palette.append(section.biomePalette.first().toUtf8());
        return out;
    }
    if (section.biomeIndices.size() != kBiomeCells) {
        return std::nullopt;
    }

    const qsizetype paletteSize = section.biomePalette.size();
    QVarLengthArray<quint8, 64> used(paletteSize, 0);
    for (uint32_t idx : section.biomeIndices) {
        // Unlike a block, a biome has no "air" to read a bad index as.
        if (idx >= static_cast<uint32_t>(paletteSize)) {
            return std::nullopt;
        }
        used[idx] = 1;
    }
    QVarLengthArray<QByteArray, 8> names(paletteSize);
    for (qsizetype i = 0; i < paletteSize; ++i) {
        if (used[i]) {
            names[i] = section.biomePalette[i].toUtf8();
            out.palette.append(names[i]);
        }
    }
    std::sort(out.palette.begin(), out.palette.end());
    out.palette.erase(std::unique(out.palette.begin(), out.palette.end()), out.palette.end());
    if (out.palette.size() == 1) {
        return out;
    }

    QVarLengthArray<quint8, 64> remap(paletteSize, 0);
    for (qsizetype i = 0; i < paletteSize; ++i) {
        if (used[i]) {
            auto it = std::lower_bound(out.palette.cbegin(), out.palette.cend(), names[i]);
            remap[i] = static_cast<quint8>(it - out.palette.cbegin());
        }
    }
    out.indices.resize(kBiomeCells);
    for (int i = 0; i < kBiomeCells; ++i) {
        out.indices[i] = remap[section.biomeIndices[i]];
    }
    return out;
}

std::optional<QByteArray> canonicalNbt(const BlockEntityData &be)
{
    nbt::tag_compound compound = NBTSerializer::blockEntityToNBT(be);
    if (compound.size() == 0) {
        return std::nullopt;
    }
    // tag_compound is a std::map, so writing it back is what sorts the keys.
    std::ostringstream os(std::ios::binary);
    nbt::io::stream_writer(os).write_payload(compound);
    const std::string bytes = os.str();
    return QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

CanonicalColumn canonicalize(qint32 chunkX, qint32 chunkZ, const QMap<int32_t, ChunkSection> &sections,
                             const QVector<BlockEntityData> &blockEntities)
{
    CanonicalColumn out;
    for (const ChunkSection &section : sections) {
        if (out.sections.size() == 255) {
            break;
        }
        if (auto biomes = canonicalBiomes(section)) {
            out.sections.append(std::move(*biomes));
        }
    }
    for (const BlockEntityData &be : blockEntities) {
        if ((be.x >> 4) != chunkX || (be.z >> 4) != chunkZ) {
            continue;
        }
        std::optional<QByteArray> nbt = canonicalNbt(be);
        if (!nbt) {
            continue;
        }
        out.blockEntities.append({be.x, be.y, be.z, be.contentsSeen ? kContentsSeen : quint8(0), std::move(*nbt)});
    }
    std::sort(out.blockEntities.begin(), out.blockEntities.end(), [](const BlockEntity &a, const BlockEntity &b) {
        return std::tie(a.y, a.z, a.x) < std::tie(b.y, b.z, b.x);
    });
    return out;
}

QByteArray encodeBlob(const CanonicalColumn &column)
{
    QByteArray out;
    qsizetype size = 2 + 4;
    for (const Biomes &b : column.sections) {
        size += 2 + b.indices.size();
        for (const QByteArray &name : b.palette) {
            size += 2 + name.size();
        }
    }
    for (const BlockEntity &be : column.blockEntities) {
        size += 12 + 1 + 4 + be.nbt.size();
    }
    out.reserve(size);

    out.append(static_cast<char>(kFormat));
    out.append(static_cast<char>(column.sections.size()));
    for (const Biomes &b : column.sections) {
        out.append(static_cast<char>(static_cast<qint8>(b.sectionY)));
        out.append(static_cast<char>(b.palette.size()));
        for (const QByteArray &name : b.palette) {
            appendU16le(out, static_cast<quint16>(name.size()));
            out.append(name);
        }
        if (b.palette.size() > 1) {
            out.append(reinterpret_cast<const char *>(b.indices.constData()), b.indices.size());
        }
    }
    appendU32le(out, static_cast<quint32>(column.blockEntities.size()));
    for (const BlockEntity &be : column.blockEntities) {
        appendU32le(out, static_cast<quint32>(be.x));
        appendU32le(out, static_cast<quint32>(be.y));
        appendU32le(out, static_cast<quint32>(be.z));
        out.append(static_cast<char>(be.flags));
        appendU32le(out, static_cast<quint32>(be.nbt.size()));
        out.append(be.nbt);
    }
    return out;
}

QByteArray digest(const QByteArray &blob, QByteArrayView prefix)
{
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    hash.addData(prefix);
    hash.addData(blob);
    return hash.result();
}

QByteArray encodeExport(const QVector<ColumnFrame> &frames)
{
    QByteArray out;
    appendU32le(out, static_cast<quint32>(frames.size()));
    for (const ColumnFrame &frame : frames) {
        appendU16le(out, static_cast<quint16>(frame.dimensionUtf8.size()));
        out.append(frame.dimensionUtf8);
        appendU32le(out, static_cast<quint32>(frame.chunkX));
        appendU32le(out, static_cast<quint32>(frame.chunkZ));
        appendU32le(out, static_cast<quint32>(frame.blob.size()));
        out.append(frame.blob);
    }
    return out;
}

}
