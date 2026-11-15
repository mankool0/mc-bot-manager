#include "ChunkWireDecoder.h"

#include <algorithm>
#include <cstdint>

using mankool::mcbot::protocol::ChunkDataMessage;
using mankool::mcbot::protocol::ChunkSection;

namespace {

// Field numbers, from proto/protocol.proto and proto/world.proto.
constexpr quint32 kMessageId = 1;
constexpr quint32 kTimestamp = 2;
constexpr quint32 kChunkData = 18;

enum WireType : quint32 { Varint = 0, Fixed64 = 1, Delimited = 2, Fixed32 = 5 };

struct Reader {
    const quint8 *p;
    const quint8 *end;

    bool atEnd() const { return p == end; }

    bool varint(quint64 &out)
    {
        quint64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p == end)
                return false;
            const quint8 b = *p++;
            // The tenth byte holds bit 63 alone.
            if (shift == 63 && b > 1)
                return false;
            v |= quint64(b & 0x7f) << shift;
            if (!(b & 0x80)) {
                out = v;
                return true;
            }
        }
        return false;
    }

    bool tag(quint32 &field, quint32 &wire)
    {
        quint64 t;
        if (!varint(t) || t > 0xffffffffu)
            return false;
        field = quint32(t >> 3);
        wire = quint32(t & 7);
        return field != 0;
    }

    bool delimited(Reader &out)
    {
        quint64 n;
        if (!varint(n) || n > quint64(end - p))
            return false;
        out = {p, p + n};
        p += n;
        return true;
    }

    bool skip(quint32 wire)
    {
        quint64 v;
        Reader r;
        switch (wire) {
        case Varint: return varint(v);
        case Delimited: return delimited(r);
        case Fixed64: if (end - p < 8) return false; p += 8; return true;
        case Fixed32: if (end - p < 4) return false; p += 4; return true;
        default: return false;
        }
    }
};

// int32 and bool as proto3 reads them: the low 32 bits, and any nonzero value.
bool readInt32(Reader &r, quint32 wire, qint32 &out)
{
    quint64 v;
    if (wire != Varint || !r.varint(v))
        return false;
    out = qint32(quint32(v));
    return true;
}

bool readBool(Reader &r, quint32 wire, bool &out)
{
    quint64 v;
    if (wire != Varint || !r.varint(v))
        return false;
    out = v != 0;
    return true;
}

// A repeated uint32, packed or one value at a time: a parser must take both. The format
// appends a second packed run to the first; the serializer replaces the list with it, so a
// packed run that is not the field's only occurrence is declined. `packed`: one was read.
bool readUint32s(Reader &r, quint32 wire, QtProtobuf::uint32List &out, bool &packed)
{
    quint64 v;
    if (packed)
        return false;
    if (wire == Varint) {
        if (!r.varint(v))
            return false;
        out.append(quint32(v));
        return true;
    }
    Reader run;
    if (wire != Delimited || !out.isEmpty() || !r.delimited(run))
        return false;
    packed = true;
    // One varint ends at each byte under 0x80.
    qsizetype count = 0;
    for (const quint8 *q = run.p; q != run.end; ++q)
        count += *q < 0x80;
    if (count == run.end - run.p) {
        // Every value one byte, as block indices nearly always are.
        out.resize(count);
        std::copy(run.p, run.end, out.data());
        return true;
    }
    out.reserve(count);
    while (!run.atEnd()) {
        if (!run.varint(v))
            return false;
        out.append(quint32(v));
    }
    return true;
}

bool readBytes(Reader &r, quint32 wire, QByteArray &out)
{
    Reader s;
    if (wire != Delimited || !r.delimited(s))
        return false;
    out = QByteArray(reinterpret_cast<const char *>(s.p), s.end - s.p);
    return true;
}

bool readString(Reader &r, quint32 wire, QString &out)
{
    Reader s;
    if (wire != Delimited || !r.delimited(s))
        return false;
    out = QString::fromUtf8(reinterpret_cast<const char *>(s.p), s.end - s.p);
    return true;
}

bool readSection(Reader r, ChunkSection &section)
{
    qint32 sectionY = 0;
    bool uniform = false, biomeUniform = false;
    QtProtobuf::uint32List palette, blockIndices, biomeIndices;
    bool palettePacked = false, indicesPacked = false, biomesPacked = false;
    QStringList biomePalette;
    QByteArray blockLight, skyLight;
    while (!r.atEnd()) {
        quint32 field, wire;
        if (!r.tag(field, wire))
            return false;
        bool ok;
        switch (field) {
        case 1: ok = readInt32(r, wire, sectionY); break;
        case 2: ok = readUint32s(r, wire, palette, palettePacked); break;
        case 3: ok = readUint32s(r, wire, blockIndices, indicesPacked); break;
        case 4: ok = readBool(r, wire, uniform); break;
        case 5: {
            QString s;
            ok = readString(r, wire, s);
            biomePalette.append(s);
            break;
        }
        case 6: ok = readUint32s(r, wire, biomeIndices, biomesPacked); break;
        case 7: ok = readBool(r, wire, biomeUniform); break;
        case 8: ok = readBytes(r, wire, blockLight); break;
        case 9: ok = readBytes(r, wire, skyLight); break;
        default: ok = false;  // unknown to this schema: the serializer keeps those
        }
        if (!ok)
            return false;
    }
    section.setSectionY(sectionY);
    section.setPalette(std::move(palette));
    section.setBlockIndices(std::move(blockIndices));
    section.setUniform(uniform);
    section.setBiomePalette(std::move(biomePalette));
    section.setBiomeIndices(std::move(biomeIndices));
    section.setBiomeUniform(biomeUniform);
    section.setBlockLight(std::move(blockLight));
    section.setSkyLight(std::move(skyLight));
    return true;
}

bool readChunk(Reader r, ChunkDataMessage &chunk)
{
    qint32 chunkX = 0, chunkZ = 0, minY = 0, maxY = 0;
    QString dimension;
    QList<ChunkSection> sections;
    QByteArrayList blockEntityNbt;
    while (!r.atEnd()) {
        quint32 field, wire;
        if (!r.tag(field, wire))
            return false;
        bool ok;
        switch (field) {
        case 1: ok = readInt32(r, wire, chunkX); break;
        case 2: ok = readInt32(r, wire, chunkZ); break;
        case 3: ok = readString(r, wire, dimension); break;
        case 4: ok = readInt32(r, wire, minY); break;
        case 5: ok = readInt32(r, wire, maxY); break;
        case 6: {
            Reader s;
            ChunkSection section;
            ok = wire == Delimited && r.delimited(s) && readSection(s, section);
            sections.append(std::move(section));
            break;
        }
        case 7: {
            QByteArray nbt;
            ok = readBytes(r, wire, nbt);
            blockEntityNbt.append(std::move(nbt));
            break;
        }
        default: ok = false;
        }
        if (!ok)
            return false;
    }
    chunk.setChunkX(chunkX);
    chunk.setChunkZ(chunkZ);
    chunk.setDimension(std::move(dimension));
    chunk.setMinY(minY);
    chunk.setMaxY(maxY);
    chunk.setSections(std::move(sections));
    chunk.setBlockEntityNbt(std::move(blockEntityNbt));
    return true;
}

}

namespace ChunkWireDecoder {

std::optional<ChunkDataMessage> decode(const QByteArray &message)
{
    const auto *bytes = reinterpret_cast<const quint8 *>(message.constData());
    Reader r{bytes, bytes + message.size()};
    Reader payload{nullptr, nullptr};
    bool seen = false;
    while (!r.atEnd()) {
        quint32 field, wire;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (field == kChunkData) {
            // A second copy is merged into the first by the format: the serializer's to do.
            if (wire != Delimited || seen || !r.delimited(payload))
                return std::nullopt;
            seen = true;
        } else if ((field == kMessageId && wire == Delimited) || (field == kTimestamp && wire == Varint)) {
            if (!r.skip(wire))
                return std::nullopt;
        } else {
            // Another payload, or a field this schema does not have.
            return std::nullopt;
        }
    }
    ChunkDataMessage chunk;
    if (!seen || !readChunk(payload, chunk))
        return std::nullopt;
    return chunk;
}

}
