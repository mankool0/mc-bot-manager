#include "SectionCodec.h"
#include "SectionCodecPrimitives.h"
#include "bot/WorldData.h"

#include <QCryptographicHash>
#include <QHash>
#include <QVarLengthArray>
#include <algorithm>
#include <cstring>

namespace SectionCodec {

namespace {

const QByteArray &airName()
{
    static const QByteArray name = QByteArrayLiteral("minecraft:air");
    return name;
}

// The blob ahead of its indices: u32le(palette_len) || [u16le(name_len) || utf8_name]*.
// Shared by encodeBlob and digest so the format is written down once.
qsizetype headerSize(const CanonicalSection &section)
{
    qsizetype size = 4;
    for (const QByteArray &name : section.palette) {
        size += 2 + name.size();
    }
    return size;
}

char *writeHeader(char *p, const CanonicalSection &section)
{
    p = putU32le(p, static_cast<quint32>(section.palette.size()));
    for (const QByteArray &name : section.palette) {
        p = putU16le(p, static_cast<quint16>(name.size()));
        memcpy(p, name.constData(), static_cast<size_t>(name.size()));
        p += name.size();
    }
    return p;
}

}

std::optional<CanonicalSection> canonicalize(const ChunkSection &section)
{
    CanonicalSection out;

    if (section.uniform) {
        out.palette.append(section.palette.isEmpty() ? airName() : section.palette.first().toUtf8());
        out.indices = QVector<quint16>(kSectionCells, 0);
        return out;
    }

    if (section.blockIndices.size() != kSectionCells) {
        return std::nullopt;
    }

    // Raw pointers and stack arrays throughout: indexing a non-const QVector checks for a
    // detach on every access, which in these 4096-step loops cost more than the hashing.
    const uint32_t *in = section.blockIndices.constData();
    const uint32_t paletteSize = static_cast<uint32_t>(section.palette.size());

    // Which palette entries the indices actually reference. Out-of-range
    // indices read as air, the same fallback ChunkSection::getBlock applies.
    QVarLengthArray<quint8, 256> used(paletteSize, 0);
    quint8 *usedFlags = used.data();
    bool outOfRange = false;
    for (int i = 0; i < kSectionCells; ++i) {
        const uint32_t idx = in[i];
        if (idx < paletteSize) {
            usedFlags[idx] = 1;
        } else {
            outOfRange = true;
        }
    }

    // Only referenced entries are converted; palette bloat never reaches UTF-8.
    QVarLengthArray<QByteArray, 32> names(paletteSize);
    QVector<QByteArray> sorted;
    sorted.reserve(paletteSize + 1);
    for (uint32_t i = 0; i < paletteSize; ++i) {
        if (usedFlags[i]) {
            names[i] = section.palette[i].toUtf8();
            sorted.append(names[i]);
        }
    }
    if (outOfRange) {
        sorted.append(airName());
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    auto newIndexOf = [&sorted](const QByteArray &name) -> quint16 {
        auto it = std::lower_bound(sorted.cbegin(), sorted.cend(), name);
        return static_cast<quint16>(it - sorted.cbegin());
    };

    QVarLengthArray<quint16, 256> remap(paletteSize, 0);
    for (uint32_t i = 0; i < paletteSize; ++i) {
        if (usedFlags[i]) {
            remap[i] = newIndexOf(names[i]);
        }
    }
    const quint16 *remapped = remap.constData();

    out.indices.resize(kSectionCells);
    quint16 *dst = out.indices.data();
    if (outOfRange) {
        const quint16 airIndex = newIndexOf(airName());
        for (int i = 0; i < kSectionCells; ++i) {
            dst[i] = in[i] < paletteSize ? remapped[in[i]] : airIndex;
        }
    } else {
        for (int i = 0; i < kSectionCells; ++i) {
            dst[i] = remapped[in[i]];
        }
    }
    out.palette = std::move(sorted);
    return out;
}

qsizetype blobSize(const CanonicalSection &section)
{
    return headerSize(section) + section.indices.size() * 2;
}

char *writeBlob(char *out, const CanonicalSection &section)
{
    char *p = writeHeader(out, section);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    const size_t indexBytes = static_cast<size_t>(section.indices.size()) * 2;
    memcpy(p, section.indices.constData(), indexBytes);
    return p + indexBytes;
#else
    for (quint16 idx : section.indices) {
        p = putU16le(p, idx);
    }
    return p;
#endif
}

qsizetype maxBlobSize(const ChunkSection &section)
{
    // Every palette entry at up to 3 UTF-8 bytes per UTF-16 unit, plus the air that
    // out-of-range indices add; the canonical palette is a deduplicated subset of that.
    qsizetype size = 4 + 2 + airName().size() + qsizetype(kSectionCells) * 2;
    for (const QString &name : section.palette) {
        size += 2 + name.size() * 3;
    }
    return size;
}

QByteArray encodeBlob(const CanonicalSection &section)
{
    // Written through a raw pointer into a pre-sized array: the 8192 one-byte
    // appends for the indices alone cost more than the BLAKE2b over the result.
    QByteArray out(blobSize(section), Qt::Uninitialized);
    writeBlob(out.data(), section);
    return out;
}

char *writeExportCount(char *out, quint32 frameCount)
{
    return putU32le(out, frameCount);
}

qsizetype exportFrameHeaderSize(QByteArrayView dimensionUtf8)
{
    return 2 + dimensionUtf8.size() + 12 + 4;
}

char *writeExportFrameHeader(char *out, QByteArrayView dimensionUtf8, qint32 chunkX, qint32 chunkZ,
                             qint32 sectionY, quint32 blobSize)
{
    char *p = putU16le(out, static_cast<quint16>(dimensionUtf8.size()));
    memcpy(p, dimensionUtf8.data(), static_cast<size_t>(dimensionUtf8.size()));
    p += dimensionUtf8.size();
    p = putI32le(p, chunkX);
    p = putI32le(p, chunkZ);
    p = putI32le(p, sectionY);
    return putU32le(p, blobSize);
}

QByteArray encodeExport(const QVector<SectionFrame> &frames)
{
    qsizetype total = 4;
    for (const SectionFrame &frame : frames) {
        total += exportFrameHeaderSize(frame.dimensionUtf8) + frame.blob.size();
    }

    QByteArray out(total, Qt::Uninitialized);
    char *p = writeExportCount(out.data(), static_cast<quint32>(frames.size()));
    for (const SectionFrame &frame : frames) {
        p = writeExportFrameHeader(p, frame.dimensionUtf8, frame.chunkX, frame.chunkZ, frame.sectionY,
                                   static_cast<quint32>(frame.blob.size()));
        memcpy(p, frame.blob.constData(), static_cast<size_t>(frame.blob.size()));
        p += frame.blob.size();
    }
    return out;
}

QByteArray digest(const CanonicalSection &section, QByteArrayView prefix)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // In memory the index array already is the blob's u16le tail, so the blob is hashed as
    // header + indices where they lie instead of being copied into one 8 KB buffer first.
    QVarLengthArray<char, 512> header(headerSize(section));
    writeHeader(header.data(), section);
    const QByteArrayView parts[] = {
        prefix,
        QByteArrayView(header.constData(), header.size()),
        QByteArrayView(reinterpret_cast<const char *>(section.indices.constData()), section.indices.size() * 2),
    };
    char out[32];
    const QByteArrayView hash = QCryptographicHash::hashInto(QSpan<char>(out), QSpan<const QByteArrayView>(parts),
                                                             QCryptographicHash::Blake2b_256);
    return hash.toByteArray();
#else
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    hash.addData(prefix);
    hash.addData(encodeBlob(section));
    return hash.result();
#endif
}

QByteArray uniformDigest(const QByteArray &nameUtf8, const QByteArray &prefix)
{
    // Per thread, so the workers hashing one listing never contend on it. It holds one entry
    // per uniform block name per prefix in use, a handful; the bounds only guard against a
    // caller cycling through prefixes.
    thread_local QHash<QByteArray, QHash<QByteArray, QByteArray>> memo;

    auto byPrefix = memo.find(prefix);
    if (byPrefix == memo.end()) {
        if (memo.size() >= 16) {
            memo.clear();
        }
        byPrefix = memo.insert(prefix, {});
    }
    auto it = byPrefix->constFind(nameUtf8);
    if (it != byPrefix->constEnd()) {
        return *it;
    }
    if (byPrefix->size() >= 1024) {
        byPrefix->clear();
    }
    const QByteArray computed = digest(CanonicalSection{{nameUtf8}, QVector<quint16>(kSectionCells, 0)}, prefix);
    byPrefix->insert(nameUtf8, computed);
    return computed;
}

std::optional<QByteArray> sectionDigest(const ChunkSection &section, const QByteArray &prefix)
{
    if (section.uniform) {
        return uniformDigest(section.palette.isEmpty() ? airName() : section.palette.first().toUtf8(), prefix);
    }
    auto canon = canonicalize(section);
    if (!canon) {
        return std::nullopt;
    }
    // A section flagged non-uniform whose indices all name one block canonicalizes to exactly
    // the uniform form, so it shares the memo.
    if (canon->palette.size() == 1) {
        return uniformDigest(canon->palette.first(), prefix);
    }
    return digest(*canon, prefix);
}

}
