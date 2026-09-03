#include "SectionCodec.h"
#include "SectionCodecPrimitives.h"
#include "bot/WorldData.h"

#include <QCryptographicHash>
#include <algorithm>
#include <cstring>

namespace SectionCodec {

std::optional<CanonicalSection> canonicalize(const ChunkSection &section)
{
    const QByteArray airName = QByteArrayLiteral("minecraft:air");

    CanonicalSection out;

    if (section.uniform) {
        out.palette.append(section.palette.isEmpty() ? airName : section.palette.first().toUtf8());
        out.indices = QVector<quint16>(kSectionCells, 0);
        return out;
    }

    if (section.blockIndices.size() != kSectionCells) {
        return std::nullopt;
    }

    QVector<QByteArray> oldNames;
    oldNames.reserve(section.palette.size());
    for (const QString &name : section.palette) {
        oldNames.append(name.toUtf8());
    }

    // Which palette entries the indices actually reference. Out-of-range
    // indices read as air, the same fallback ChunkSection::getBlock applies.
    QVector<bool> used(oldNames.size(), false);
    bool outOfRange = false;
    for (uint32_t idx : section.blockIndices) {
        if (idx < static_cast<uint32_t>(oldNames.size())) {
            used[idx] = true;
        } else {
            outOfRange = true;
        }
    }

    QVector<QByteArray> sorted;
    for (int i = 0; i < oldNames.size(); ++i) {
        if (used[i]) {
            sorted.append(oldNames[i]);
        }
    }
    if (outOfRange) {
        sorted.append(airName);
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    auto newIndexOf = [&sorted](const QByteArray &name) -> quint16 {
        auto it = std::lower_bound(sorted.cbegin(), sorted.cend(), name);
        return static_cast<quint16>(it - sorted.cbegin());
    };

    QVector<quint16> remap(oldNames.size(), 0);
    for (int i = 0; i < oldNames.size(); ++i) {
        if (used[i]) {
            remap[i] = newIndexOf(oldNames[i]);
        }
    }
    const quint16 airIndex = outOfRange ? newIndexOf(airName) : 0;

    out.palette = sorted;
    out.indices.reserve(kSectionCells);
    for (uint32_t idx : section.blockIndices) {
        if (idx < static_cast<uint32_t>(oldNames.size())) {
            out.indices.append(remap[idx]);
        } else {
            out.indices.append(airIndex);
        }
    }
    return out;
}

QByteArray encodeBlob(const CanonicalSection &section)
{
    qsizetype nameBytes = 0;
    for (const QByteArray &name : section.palette) {
        nameBytes += 2 + name.size();
    }

    // Written through a raw pointer into a pre-sized array: the 8192 one-byte
    // appends for the indices alone cost more than the BLAKE2b over the result.
    QByteArray out(4 + nameBytes + kSectionCells * 2, Qt::Uninitialized);
    char *p = out.data();
    p = putU32le(p, static_cast<quint32>(section.palette.size()));
    for (const QByteArray &name : section.palette) {
        p = putU16le(p, static_cast<quint16>(name.size()));
        memcpy(p, name.constData(), static_cast<size_t>(name.size()));
        p += name.size();
    }
    for (quint16 idx : section.indices) {
        p = putU16le(p, idx);
    }
    return out;
}

QByteArray encodeExport(const QVector<SectionFrame> &frames)
{
    qsizetype total = 4;
    for (const SectionFrame &frame : frames) {
        total += 2 + frame.dimensionUtf8.size() + 12 + 4 + frame.blob.size();
    }

    QByteArray out;
    out.reserve(total);
    appendU32le(out, static_cast<quint32>(frames.size()));
    for (const SectionFrame &frame : frames) {
        appendU16le(out, static_cast<quint16>(frame.dimensionUtf8.size()));
        out.append(frame.dimensionUtf8);
        appendI32le(out, frame.chunkX);
        appendI32le(out, frame.chunkZ);
        appendI32le(out, frame.sectionY);
        appendU32le(out, static_cast<quint32>(frame.blob.size()));
        out.append(frame.blob);
    }
    return out;
}

QByteArray digest(const CanonicalSection &section, QByteArrayView prefix)
{
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    hash.addData(prefix);
    hash.addData(encodeBlob(section));
    return hash.result();
}

}
