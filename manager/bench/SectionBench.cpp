// Benchmark and self-checks for the section observation path behind
// world.changed_sections / world.export_sections.
//
//   cmake --build <build dir> --target section_bench
//   <build dir>/section_bench [--checks-only] [--seconds N] [--only codec|calls|listing|python|export]
//
// Checks (always run first, exit status 1 on any failure): the section codec's known-answer
// vectors, the optimized canonicalize/digest against a straightforward reference over
// thousands of random sections, the memoized uniform digests against computed ones, parallel
// export against serial, and the dropped-section semantics of listChanges.
//
// Benchmarks: microseconds per section for canonicalize, encodeBlob and digest, whole listing
// calls of 1,000-2,000 sections, and one vs ten concurrent callers - C++ threads, then Python
// threads through a module that mirrors the production bindings and GIL handling.
//
// The terrain is synthetic but shaped like the nether a flying bot loads: sixteen sections
// per column, non-uniform ones below the roof with 5-20 palette entries, the uniform air
// above it, and a share of uniform netherrack and lava.

#include "bot/SectionDirtyTracker.h"
#include "bot/SectionObservation.h"
#include "bot/WorldData.h"
#include "world/SectionCodec.h"
#include "world/SectionCodecPrimitives.h"
#include "scripting/PythonAPI.h"

#include <QCryptographicHash>
#include <QReadWriteLock>
#include <QSet>
#include <QThread>

#include <pybind11/embed.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace py = pybind11;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

const QByteArray kPrefix = QByteArray("hiveobs1\0", 9);
const QByteArray kNether = QByteArrayLiteral("minecraft:the_nether");
const QByteArray kOverworld = QByteArrayLiteral("minecraft:overworld");

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

struct Rng {
    quint64 state;

    explicit Rng(quint64 seed) : state(seed) {}

    quint64 next()
    {
        quint64 z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    int below(int n) { return static_cast<int>(next() % static_cast<quint64>(n)); }
    double unit() { return static_cast<double>(next() >> 11) / static_cast<double>(1ULL << 53); }
};

const char *const kFiller[] = {
    "minecraft:basalt[axis=y]", "minecraft:basalt[axis=x]", "minecraft:basalt[axis=z]",
    "minecraft:blackstone", "minecraft:soul_sand", "minecraft:soul_soil", "minecraft:gravel",
    "minecraft:magma_block", "minecraft:glowstone", "minecraft:lava[level=0]", "minecraft:lava[level=2]",
    "minecraft:lava[level=4]", "minecraft:crimson_nylium", "minecraft:warped_nylium",
    "minecraft:nether_wart_block", "minecraft:shroomlight", "minecraft:bone_block[axis=y]",
    "minecraft:polished_blackstone_bricks", "minecraft:gilded_blackstone", "minecraft:obsidian",
    "minecraft:crimson_roots", "minecraft:warped_roots", "minecraft:crimson_fungus",
    "minecraft:fire[age=0,east=false,north=false,south=false,up=false,west=false]",
};
const char *const kOres[] = {
    "minecraft:nether_quartz_ore", "minecraft:nether_gold_ore", "minecraft:ancient_debris",
};

ChunkSection uniformSection(int sectionY, const QString &name)
{
    ChunkSection s;
    s.sectionY = sectionY;
    s.uniform = true;
    s.palette = {name};
    return s;
}

// Non-uniform section with `paletteSize` entries, laid out in 4x4x4 blobs (terrain comes in
// runs, which is what the index scans see) with ores sprinkled per cell. The palette is
// shuffled afterwards, as a palette grown by the game is in first-seen order, and sometimes
// carries entries no index uses.
ChunkSection mixedSection(Rng &rng, int sectionY, int paletteSize)
{
    QVector<QString> names{QStringLiteral("minecraft:netherrack"), QStringLiteral("minecraft:air")};
    if (sectionY == 0 || sectionY == 7) {
        names.append(QStringLiteral("minecraft:bedrock"));
    }
    const int firstOre = names.size();
    const int oresWanted = 1 + rng.below(3);
    for (int i = 0; i < oresWanted && names.size() < paletteSize; ++i) {
        names.append(QString::fromLatin1(kOres[i]));
    }
    const int oreCount = names.size() - firstOre;
    const int fillerCount = int(sizeof(kFiller) / sizeof(kFiller[0]));
    while (names.size() < paletteSize) {
        const QString candidate = QString::fromLatin1(kFiller[rng.below(fillerCount)]);
        if (!names.contains(candidate)) {
            names.append(candidate);
        }
    }

    const int unused = rng.unit() < 0.2 ? 1 + rng.below(2) : 0;
    const int used = std::max(firstOre + oreCount, paletteSize - unused);
    const int firstFiller = firstOre + oreCount;
    const bool hasBedrock = firstOre == 3;

    QVector<uint32_t> indices(kSectionCells);
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const int cell = y * 256 + z * 16 + x;
                Rng blob(quint64(sectionY) * 1000003ULL + quint64(y / 4) * 7919ULL + quint64(z / 4) * 104729ULL
                         + quint64(x / 4) * 15485863ULL + quint64(paletteSize));
                const double roll = blob.unit();
                int idx = 0;
                if (roll >= 0.8 && used > firstFiller) {
                    idx = firstFiller + blob.below(used - firstFiller);
                } else if (roll >= 0.55) {
                    idx = 1;
                }
                if (idx == 0 && oreCount > 0 && rng.unit() < 0.02) {
                    idx = firstOre + rng.below(oreCount);
                }
                if (hasBedrock && ((sectionY == 0 && y < 5) || (sectionY == 7 && y >= 11)) && rng.unit() < 0.6) {
                    idx = 2;
                }
                indices[cell] = static_cast<uint32_t>(idx);
            }
        }
    }
    // Every entry below `used` appears at least once, so the palette size is what was asked.
    for (int i = 0; i < used; ++i) {
        indices[rng.below(kSectionCells)] = static_cast<uint32_t>(i);
    }

    QVector<int> order(paletteSize);
    for (int i = 0; i < paletteSize; ++i) {
        order[i] = i;
    }
    for (int i = paletteSize - 1; i > 0; --i) {
        std::swap(order[i], order[rng.below(i + 1)]);
    }
    QVector<int> newPos(paletteSize);
    ChunkSection s;
    s.sectionY = sectionY;
    s.palette.resize(paletteSize);
    for (int i = 0; i < paletteSize; ++i) {
        s.palette[i] = names[order[i]];
        newPos[order[i]] = i;
    }
    s.blockIndices.resize(kSectionCells);
    for (int i = 0; i < kSectionCells; ++i) {
        s.blockIndices[i] = static_cast<uint32_t>(newPos[indices[i]]);
    }
    return s;
}

ChunkData makeColumn(Rng &rng, const QByteArray &dimension = kNether)
{
    ChunkData chunk;
    chunk.dimension = QString::fromUtf8(dimension);
    chunk.minY = 0;
    chunk.maxY = 256;
    for (int sy = 0; sy < 16; ++sy) {
        ChunkSection s;
        if (sy >= 9) {
            s = uniformSection(sy, QStringLiteral("minecraft:air"));
        } else if (sy == 8) {
            s = uniformSection(sy, QStringLiteral("minecraft:air"));
            // Mushrooms on the roof in about one chunk in twenty.
            if (rng.unit() < 0.05) {
                s.uniform = false;
                s.palette.append(QStringLiteral("minecraft:brown_mushroom"));
                s.blockIndices = QVector<uint32_t>(kSectionCells, 0);
                s.blockIndices[rng.below(256)] = 1;
            }
        } else if (sy == 1 && rng.unit() < 0.08) {
            s = uniformSection(sy, QStringLiteral("minecraft:lava[level=0]"));
        } else if (sy >= 3 && sy <= 6 && rng.unit() < 0.06) {
            s = uniformSection(sy, QStringLiteral("minecraft:netherrack"));
        } else {
            s = mixedSection(rng, sy, 5 + rng.below(16));
        }
        chunk.sections.insert(sy, s);
    }
    return chunk;
}

// A pool of distinct columns bigger than the last-level cache; every bot's world is built
// from it (implicitly shared, so ten worlds cost one pool of memory).
QVector<ChunkData> makePool(int count)
{
    Rng rng(0x5ec710a);
    QVector<ChunkData> pool;
    pool.reserve(count);
    for (int i = 0; i < count; ++i) {
        pool.append(makeColumn(rng));
    }
    return pool;
}

struct Bot {
    BotWorldData world;
    QReadWriteLock lock;
    SectionDirtyTracker tracker;
    QVector<SectionColumnKey> columns;
    int nextColumn = 0;
    std::optional<quint64> token;
};

std::unique_ptr<Bot> makeBot(const QVector<ChunkData> &pool, int botIndex, int columnCount)
{
    auto bot = std::make_unique<Bot>();
    for (int i = 0; i < columnCount; ++i) {
        ChunkData chunk = pool[(i * 7 + botIndex * 131) % pool.size()];
        chunk.chunkX = botIndex * 100000 + i;
        chunk.chunkZ = botIndex;
        bot->world.loadChunk(chunk);
        bot->columns.append({chunk.chunkX, chunk.chunkZ});
    }
    return bot;
}

// The next `sections` worth of whole columns, marked the way a chunk load marks them.
int markNext(Bot &bot, int sections)
{
    const int columnCount = std::max(1, sections / 16);
    QVector<SectionKey> keys;
    keys.reserve(columnCount * 16);
    for (int c = 0; c < columnCount; ++c) {
        const SectionColumnKey col = bot.columns[bot.nextColumn];
        bot.nextColumn = (bot.nextColumn + 1) % bot.columns.size();
        for (int sy = 0; sy < 16; ++sy) {
            keys.append({col.chunkX, col.chunkZ, sy});
        }
    }
    bot.tracker.markAll(keys);
    return keys.size();
}

SectionObservation::Changes list(Bot &bot, std::optional<quint64> since, const QByteArray &dimension = kNether,
                                 bool digest = true, int limit = 0)
{
    return SectionObservation::listChanges(bot.tracker, bot.world, bot.lock, since, dimension, digest, limit, kPrefix);
}

// ---------------------------------------------------------------------------
// Reference codec: canonicalize as it was before the listing path was optimized, kept
// verbatim, and the blob written out byte by byte as the format spells it. The optimized
// codec must match them byte for byte; the benchmarks run the old code as the "before".
// ---------------------------------------------------------------------------

std::optional<SectionCodec::CanonicalSection> referenceCanonicalize(const ChunkSection &section)
{
    const QByteArray airName = QByteArrayLiteral("minecraft:air");

    SectionCodec::CanonicalSection out;

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

// The blob byte by byte, as the format spells it: independent of how the codec lays it out.
QByteArray referenceEncodeBlob(const SectionCodec::CanonicalSection &section)
{
    QByteArray out;
    auto u16 = [&out](quint16 v) {
        out.append(static_cast<char>(v & 0xff));
        out.append(static_cast<char>(v >> 8));
    };
    const quint32 count = static_cast<quint32>(section.palette.size());
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(static_cast<char>((count >> shift) & 0xff));
    }
    for (const QByteArray &name : section.palette) {
        u16(static_cast<quint16>(name.size()));
        out.append(name);
    }
    for (quint16 idx : section.indices) {
        u16(idx);
    }
    return out;
}

QByteArray referenceDigest(const SectionCodec::CanonicalSection &section, const QByteArray &prefix)
{
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    hash.addData(prefix);
    hash.addData(referenceEncodeBlob(section));
    return hash.result();
}

// encodeBlob and digest as they were before, verbatim, for the "before" timings.
QByteArray beforeEncodeBlob(const SectionCodec::CanonicalSection &section)
{
    qsizetype nameBytes = 0;
    for (const QByteArray &name : section.palette) {
        nameBytes += 2 + name.size();
    }

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

QByteArray beforeDigest(const SectionCodec::CanonicalSection &section, const QByteArray &prefix)
{
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    hash.addData(prefix);
    hash.addData(beforeEncodeBlob(section));
    return hash.result();
}

// The listing and the export as they ran before: every section canonicalized and hashed (or
// encoded) one after another on the caller's thread with the reference codec.
SectionObservation::Changes listBefore(Bot &bot, std::optional<quint64> since, bool digest)
{
    SectionObservation::Changes result;
    QVector<SectionKey> keys;
    result.token = bot.tracker.snapshot(since, 0, keys).token;
    QVector<std::pair<SectionKey, ChunkSection>> pending;
    {
        QReadLocker locker(&bot.lock);
        for (const SectionKey &key : std::as_const(keys)) {
            const ChunkData *chunk = bot.world.getChunk(key.chunkX, key.chunkZ);
            if (chunk && chunk->dimension.toUtf8() == kNether && chunk->sections.contains(key.sectionY)) {
                pending.append({key, chunk->sections.value(key.sectionY)});
            }
        }
    }
    for (const auto &[key, section] : std::as_const(pending)) {
        QByteArray d;
        if (digest) {
            d = beforeDigest(*referenceCanonicalize(section), kPrefix);
        }
        result.sections.append({key, d});
    }
    return result;
}

QByteArray exportBefore(Bot &bot, const QVector<SectionKey> &keys)
{
    QVector<std::pair<SectionKey, ChunkSection>> pending;
    {
        QReadLocker locker(&bot.lock);
        for (const SectionKey &key : keys) {
            const ChunkData *chunk = bot.world.getChunk(key.chunkX, key.chunkZ);
            if (chunk && chunk->dimension.toUtf8() == kNether && chunk->sections.contains(key.sectionY)) {
                pending.append({key, chunk->sections.value(key.sectionY)});
            }
        }
    }
    QVector<SectionCodec::SectionFrame> frames;
    for (const auto &[key, section] : std::as_const(pending)) {
        frames.append({kNether, key.chunkX, key.chunkZ, key.sectionY,
                       beforeEncodeBlob(*referenceCanonicalize(section))});
    }
    return SectionCodec::encodeExport(frames);
}

// Random sections aimed at the corners the terrain generator never reaches: duplicate names
// in one palette, indices past the palette end, empty palettes, one-name non-uniform sections,
// palettes past 256 entries.
ChunkSection randomSection(Rng &rng)
{
    ChunkSection s;
    const int kind = rng.below(10);
    if (kind == 0) {
        s.uniform = true;
        if (rng.below(4) != 0) {
            s.palette = {QString::fromLatin1(kFiller[rng.below(int(sizeof(kFiller) / sizeof(kFiller[0])))])};
        }
        return s;
    }
    const int paletteSize = kind == 1 ? 0 : kind == 2 ? 1 : kind == 3 ? 300 + rng.below(200) : 1 + rng.below(24);
    for (int i = 0; i < paletteSize; ++i) {
        const int pick = rng.below(40);
        s.palette.append(pick < 24 ? QString::fromLatin1(kFiller[pick])
                         : pick < 30 ? QStringLiteral("minecraft:air")
                                     : QStringLiteral("minecraft:block_%1").arg(rng.below(1000)));
    }
    const double outOfRange = rng.below(3) == 0 ? 0.01 : 0.0;
    s.blockIndices.resize(kSectionCells);
    for (int i = 0; i < kSectionCells; ++i) {
        const bool far = paletteSize == 0 || rng.unit() < outOfRange;
        s.blockIndices[i] = far ? static_cast<uint32_t>(paletteSize + rng.below(5))
                                : static_cast<uint32_t>(rng.below(std::max(1, paletteSize)));
    }
    return s;
}

QByteArray hex(const QByteArray &bytes)
{
    return bytes.toHex();
}

void checkKnownAnswers()
{
    std::printf("known-answer vectors (prefix \"hiveobs1\\0\"):\n");

    const ChunkSection air = uniformSection(0, QStringLiteral("minecraft:air"));
    const QByteArray airVector = "6e486e74b975a44889dcbbcafb07a99d84a3f6fbb72559f298eed6d9e863c05e";
    auto canonAir = SectionCodec::canonicalize(air);
    check(canonAir && hex(SectionCodec::digest(*canonAir, kPrefix)) == airVector, "uniform minecraft:air");
    check(hex(SectionCodec::sectionDigest(air, kPrefix).value_or(QByteArray())) == airVector,
          "uniform minecraft:air through sectionDigest (memoized)");

    ChunkSection three;
    three.palette = {QStringLiteral("minecraft:netherrack"), QStringLiteral("minecraft:obsidian"),
                     QStringLiteral("minecraft:air")};
    three.blockIndices.resize(kSectionCells);
    for (int i = 0; i < kSectionCells; ++i) {
        three.blockIndices[i] = static_cast<uint32_t>(i % 3);
    }
    const QByteArray threeVector = "52c80fddbcfa027680bef260300d064f9740a798f0db3fe1e10404abf32df991";
    auto canonThree = SectionCodec::canonicalize(three);
    check(canonThree && hex(SectionCodec::digest(*canonThree, kPrefix)) == threeVector,
          "[netherrack, obsidian, air], index i % 3");
    check(hex(SectionCodec::sectionDigest(three, kPrefix).value_or(QByteArray())) == threeVector,
          "[netherrack, obsidian, air], index i % 3 through sectionDigest");
}

void checkMemo()
{
    std::printf("memoized uniform digests:\n");
    const QByteArray prefixes[] = {kPrefix, QByteArray(), QByteArrayLiteral("other")};
    const QString names[] = {QStringLiteral("minecraft:air"), QStringLiteral("minecraft:netherrack"),
                             QStringLiteral("minecraft:lava[level=0]")};
    bool allMatch = true;
    for (const QByteArray &prefix : prefixes) {
        for (const QString &name : names) {
            const ChunkSection s = uniformSection(0, name);
            const QByteArray computed = referenceDigest(*referenceCanonicalize(s), prefix);
            // Twice: the first call fills the memo, the second is served from it.
            const QByteArray first = SectionCodec::sectionDigest(s, prefix).value_or(QByteArray());
            const QByteArray second = SectionCodec::sectionDigest(s, prefix).value_or(QByteArray());
            allMatch = allMatch && first == computed && second == computed
                       && SectionCodec::uniformDigest(name.toUtf8(), prefix) == computed;
        }
    }
    check(allMatch, "air, netherrack and lava under hiveobs1\\0, empty and another prefix match computed digests");

    const QByteArray netherrack = referenceDigest(*referenceCanonicalize(uniformSection(0, QStringLiteral("minecraft:netherrack"))), kPrefix);
    check(SectionCodec::uniformDigest(QByteArrayLiteral("minecraft:netherrack"), kPrefix) == netherrack
              && netherrack != SectionCodec::uniformDigest(QByteArrayLiteral("minecraft:air"), kPrefix),
          "netherrack memo is its own digest, not air's");

    // Non-uniform flag, one name in use: served from the same memo.
    ChunkSection oneName;
    oneName.palette = {QStringLiteral("minecraft:netherrack"), QStringLiteral("minecraft:air")};
    oneName.blockIndices = QVector<uint32_t>(kSectionCells, 0);
    check(SectionCodec::sectionDigest(oneName, kPrefix) == netherrack,
          "non-uniform section of one block digests as the uniform one");

    // Memo entries are per thread; a digest made on another thread must agree.
    QByteArray fromThread;
    std::thread([&] {
        fromThread = SectionCodec::sectionDigest(uniformSection(0, QStringLiteral("minecraft:air")), kPrefix)
                         .value_or(QByteArray());
    }).join();
    check(hex(fromThread) == "6e486e74b975a44889dcbbcafb07a99d84a3f6fbb72559f298eed6d9e863c05e",
          "memo on a fresh thread gives the air vector");
}

void checkEquivalence(const QVector<ChunkData> &pool)
{
    std::printf("optimized codec against the reference:\n");
    Rng rng(42);
    int compared = 0;
    bool canonOk = true;
    bool blobOk = true;
    bool digestOk = true;
    bool sectionDigestOk = true;
    auto compare = [&](const ChunkSection &s) {
        const auto ref = referenceCanonicalize(s);
        const auto got = SectionCodec::canonicalize(s);
        if (ref.has_value() != got.has_value()) {
            canonOk = false;
            return;
        }
        if (!ref) {
            sectionDigestOk = sectionDigestOk && !SectionCodec::sectionDigest(s, kPrefix);
            return;
        }
        canonOk = canonOk && ref->palette == got->palette && ref->indices == got->indices;
        blobOk = blobOk && SectionCodec::encodeBlob(*got) == referenceEncodeBlob(*ref)
                 && beforeEncodeBlob(*ref) == referenceEncodeBlob(*ref);
        const QByteArray expected = referenceDigest(*ref, kPrefix);
        digestOk = digestOk && SectionCodec::digest(*got, kPrefix) == expected;
        sectionDigestOk = sectionDigestOk && SectionCodec::sectionDigest(s, kPrefix) == expected;
        ++compared;
    };
    for (int i = 0; i < 5000; ++i) {
        compare(randomSection(rng));
    }
    ChunkSection shortIndices;
    shortIndices.palette = {QStringLiteral("minecraft:air")};
    shortIndices.blockIndices = QVector<uint32_t>(100, 0);
    compare(shortIndices);
    for (int c = 0; c < 64; ++c) {
        for (const ChunkSection &s : pool[c].sections) {
            compare(s);
        }
    }
    std::printf("  (%d sections compared)\n", compared);
    check(canonOk, "canonicalize: identical palette and indices");
    check(blobOk, "encodeBlob (and the old one the timings use): identical to the blob written out byte by byte");
    check(digestOk, "digest: identical to BLAKE2b(prefix || blob)");
    check(sectionDigestOk, "sectionDigest: identical to the reference digest");
}

void checkExport(const QVector<ChunkData> &pool)
{
    std::printf("export_sections:\n");
    auto bot = makeBot(pool, 0, 256);
    QVector<SectionKey> keys;
    for (int c = 0; c < 256; ++c) {
        for (int sy = 0; sy < 16; ++sy) {
            keys.append({bot->columns[c].chunkX, bot->columns[c].chunkZ, sy});
        }
    }
    keys.append({123456789, 0, 0});  // not loaded: omitted
    // A section that cannot be encoded, mid-batch: omitted, and the frames after it close up.
    {
        ChunkData chunk = pool[1];
        chunk.chunkX = 200000;
        chunk.chunkZ = 0;
        chunk.sections[4].uniform = false;
        chunk.sections[4].blockIndices = QVector<uint32_t>(100, 0);
        bot->world.loadChunk(chunk);
        for (int sy = 0; sy < 16; ++sy) {
            keys.insert(2048 + sy, {200000, 0, sy});
        }
    }
    const QByteArray parallel = SectionObservation::exportSections(bot->world, bot->lock, keys, kNether);

    QVector<SectionCodec::SectionFrame> frames;
    for (const SectionKey &key : std::as_const(keys)) {
        const ChunkData *chunk = bot->world.getChunk(key.chunkX, key.chunkZ);
        const auto canon = chunk ? referenceCanonicalize(chunk->sections.value(key.sectionY)) : std::nullopt;
        if (canon) {
            frames.append({kNether, key.chunkX, key.chunkZ, key.sectionY, referenceEncodeBlob(*canon)});
        }
    }
    check(frames.size() == 4096 + 15 && parallel == SectionCodec::encodeExport(frames),
          "4111 sections exported in parallel match serial, in order, past an unencodable one");
    check(SectionObservation::exportSections(bot->world, bot->lock, keys, kOverworld).size() == 4,
          "a dimension that does not match exports zero frames");
}

// What BotManager does on a chunk load and a chunk unload.
void loadColumn(Bot &bot, ChunkData chunk, qint32 chunkX, qint32 chunkZ)
{
    chunk.chunkX = chunkX;
    chunk.chunkZ = chunkZ;
    QVector<SectionKey> keys;
    for (auto it = chunk.sections.cbegin(); it != chunk.sections.cend(); ++it) {
        keys.append({chunkX, chunkZ, it.key()});
    }
    {
        QWriteLocker locker(&bot.lock);
        bot.world.loadChunk(chunk);
    }
    bot.tracker.markAll(keys);
}

void unloadColumn(Bot &bot, qint32 chunkX, qint32 chunkZ)
{
    QWriteLocker locker(&bot.lock);
    const ChunkData *chunk = bot.world.getChunk(chunkX, chunkZ);
    bot.tracker.dropColumn(chunkX, chunkZ, chunk ? chunk->dimension.toUtf8() : QByteArray());
    bot.world.unloadChunk(chunkX, chunkZ);
}

QSet<SectionKey> columnKeys(qint32 chunkX, qint32 chunkZ)
{
    QSet<SectionKey> keys;
    for (int sy = 0; sy < 16; ++sy) {
        keys.insert({chunkX, chunkZ, sy});
    }
    return keys;
}

QSet<SectionKey> asSet(const QVector<SectionKey> &keys)
{
    return QSet<SectionKey>(keys.cbegin(), keys.cend());
}

void checkDropped(const QVector<ChunkData> &pool)
{
    std::printf("dropped sections:\n");
    Bot bot;
    const ChunkData nether = pool[0];
    Rng rng(7);
    const ChunkData overworld = makeColumn(rng, kOverworld);

    // Poller 2 takes its token first and then sleeps through everything below.
    const quint64 poller2 = list(bot, std::nullopt).token;
    const quint64 t0 = list(bot, std::nullopt).token;

    loadColumn(bot, nether, 1, 0);
    unloadColumn(bot, 1, 0);
    auto r = list(bot, t0);
    check(r.sections.isEmpty() && asSet(r.dropped) == columnKeys(1, 0) && r.droppedTotal == 16
              && !r.droppedIncomplete,
          "column marked then unloaded before the poll: its 16 sections are dropped");
    const quint64 t1 = r.token;
    r = list(bot, t1);
    check(r.dropped.isEmpty() && r.droppedTotal == 0, "and reported once: the next poll is clean");

    loadColumn(bot, nether, 2, 0);
    r = list(bot, t1);
    check(r.sections.size() == 16 && r.dropped.isEmpty(), "column listed while loaded");
    const quint64 t2 = r.token;
    unloadColumn(bot, 2, 0);
    r = list(bot, t2);
    check(r.dropped.isEmpty() && r.droppedTotal == 0, "column unloaded after a poll that reported it: not dropped");

    r = list(bot, poller2);
    check(r.sections.isEmpty() && asSet(r.dropped) == (columnKeys(1, 0) | columnKeys(2, 0)) && r.droppedTotal == 32,
          "a second poller with an older token gets its own answer (both columns)");
    r = list(bot, t2);
    check(r.dropped.isEmpty(), "and polling never consumed the first poller's answer");

    loadColumn(bot, nether, 3, 0);
    bot.tracker.mark(3, 0, 5);
    bot.tracker.mark(3, 0, 5);
    unloadColumn(bot, 3, 0);
    r = list(bot, r.token);
    check(r.droppedTotal == 16 && r.dropped.size() == 16, "re-marked (block update) then unloaded: counted once");

    const quint64 t3 = r.token;
    loadColumn(bot, nether, 4, 0);
    unloadColumn(bot, 4, 0);
    loadColumn(bot, nether, 4, 0);
    r = list(bot, t3);
    check(r.sections.size() == 16 && r.dropped.isEmpty(),
          "dropped, then reloaded before the poll: listed, not dropped");
    unloadColumn(bot, 4, 0);

    const quint64 t4 = list(bot, std::nullopt).token;
    loadColumn(bot, overworld, 5, 0);
    unloadColumn(bot, 5, 0);
    check(list(bot, t4, kNether).dropped.isEmpty(), "an overworld drop is not reported to a nether poll");
    check(asSet(list(bot, t4, kOverworld).dropped) == columnKeys(5, 0), "it is reported to an overworld poll");
    check(asSet(list(bot, t4, QByteArray()).dropped) == columnKeys(5, 0), "and to an unfiltered one");

    const quint64 t5 = list(bot, std::nullopt).token;
    loadColumn(bot, overworld, 6, 0);
    unloadColumn(bot, 6, 0);
    loadColumn(bot, nether, 6, 0);
    check(asSet(list(bot, t5, kOverworld).dropped) == columnKeys(6, 0),
          "overworld column replaced by a nether one at the same coordinates: still dropped for overworld");

    const quint64 t6 = list(bot, std::nullopt).token;
    loadColumn(bot, nether, 7, 0);
    loadColumn(bot, nether, 8, 0);
    loadColumn(bot, nether, 10, 0);
    unloadColumn(bot, 8, 0);
    r = list(bot, t6, kNether, true, 16);
    check(r.truncated && r.sections.size() == 16 && r.dropped.isEmpty(),
          "truncated poll: drops past its token wait for the next poll");
    r = list(bot, r.token);
    check(r.sections.size() == 16 && asSet(r.dropped) == columnKeys(8, 0), "which reports them");

    const quint64 t7 = r.token;
    loadColumn(bot, nether, 9, 0);
    bot.tracker.clear([&bot](qint32 chunkX, qint32 chunkZ) {
        const ChunkData *chunk = bot.world.getChunk(chunkX, chunkZ);
        return chunk ? chunk->dimension.toUtf8() : QByteArray();
    });
    {
        QWriteLocker locker(&bot.lock);
        bot.world.clearWorldState();
    }
    r = list(bot, t7);
    check(asSet(r.dropped).contains(columnKeys(9, 0)) && !r.droppedIncomplete,
          "a world reset (disconnect) reports what was pending as dropped");

    r = list(bot, std::nullopt);
    check(r.dropped.isEmpty() && !r.droppedIncomplete, "no token: no drop history to report");
    SectionDirtyTracker other;
    SectionObservation::Changes foreign = SectionObservation::listChanges(
        other, bot.world, bot.lock, t7, kNether, false, 0, kPrefix);
    check(foreign.dropped.isEmpty() && foreign.droppedIncomplete, "another tracker's token: dropped_incomplete");

    const quint64 tMalformed = list(bot, std::nullopt).token;
    {
        ChunkData chunk = nether;
        chunk.sections[4].uniform = false;
        chunk.sections[4].blockIndices = QVector<uint32_t>(100, 0);
        loadColumn(bot, chunk, 11, 0);
    }
    r = list(bot, tMalformed);
    check(r.sections.size() == 15 && r.dropped == QVector<SectionKey>{{11, 0, 4}},
          "a section that cannot be encoded is reported as dropped, not skipped silently");
    check(list(bot, tMalformed, kNether, false).sections.size() == 16, "without digests it is still listed");

    // Overflow the log: more drops than it holds, all after t8.
    const quint64 t8 = list(bot, std::nullopt).token;
    const int columns = 32768 / 16 + 100;
    for (int c = 0; c < columns; ++c) {
        loadColumn(bot, nether, 1000 + c, 0);
        unloadColumn(bot, 1000 + c, 0);
    }
    r = list(bot, t8);
    check(r.droppedIncomplete && r.dropped.size() == SectionObservation::kMaxDroppedKeys
              && r.droppedTotal == 32768,
          "log overflow: dropped_incomplete, list capped at 4096, total is what the log still holds");
    const quint64 t9 = r.token;
    loadColumn(bot, nether, 50000, 0);
    unloadColumn(bot, 50000, 0);
    r = list(bot, t9);
    check(!r.droppedIncomplete && r.droppedTotal == 16, "a token newer than the evicted entries is exact again");
}

// The promise under races: with a writer loading, updating and unloading columns while a
// poller polls, every section marked after the poller's first token reaches it, in
// `sections` or in `dropped`.
void checkNoSilentDrops(const QVector<ChunkData> &pool)
{
    std::printf("no silent drops under concurrent loads and unloads:\n");
    bool allSeen = true;
    bool neverIncomplete = true;
    qsizetype droppedSeen = 0;
    for (int round = 0; round < 3; ++round) {
        Bot bot;
        const quint64 first = list(bot, std::nullopt).token;
        std::atomic<bool> writing{true};
        QSet<SectionKey> marked;
        QSet<SectionKey> seen;

        // Every load is a new column, so a section is never listed thanks to a later reload of
        // its coordinates: seeing it proves this particular life of it was reported.
        std::thread writer([&] {
            Rng rng(1000 + round);
            QVector<int> loaded;
            int next = 0;
            for (int op = 0; op < 1500; ++op) {
                const int roll = rng.below(10);
                if (roll < 5 || loaded.isEmpty()) {
                    loadColumn(bot, pool[next % pool.size()], next, round);
                    marked.unite(columnKeys(next, round));
                    loaded.append(next++);
                } else if (roll < 8) {
                    const int at = rng.below(loaded.size());
                    unloadColumn(bot, loaded[at], round);
                    loaded.remove(at);
                } else {
                    bot.tracker.mark(loaded[rng.below(loaded.size())], round, rng.below(16));
                }
                if (op % 10 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
            writing = false;
        });

        std::optional<quint64> token = first;
        bool last = false;
        int polls = 0;
        while (!last) {
            last = !writing.load();
            const SectionObservation::Changes r = list(bot, token, kNether, polls % 2 == 0);
            for (const SectionObservation::Change &c : r.sections) {
                seen.insert(c.key);
            }
            for (const SectionKey &key : r.dropped) {
                seen.insert(key);
            }
            droppedSeen += r.droppedTotal;
            neverIncomplete = neverIncomplete && !r.droppedIncomplete;
            token = r.token;
            ++polls;
            // Slower than the writer, so columns load and unload between polls.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        writer.join();
        const SectionObservation::Changes r = list(bot, token);
        for (const SectionObservation::Change &c : r.sections) {
            seen.insert(c.key);
        }
        for (const SectionKey &key : r.dropped) {
            seen.insert(key);
        }
        allSeen = allSeen && seen.contains(marked);
    }
    std::printf("  (%lld drops reported across 3 rounds)\n", static_cast<long long>(droppedSeen));
    check(allSeen, "every marked section was listed or reported dropped");
    check(neverIncomplete, "and no poll had to say its count was incomplete");
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

void benchCodec(const QVector<ChunkData> &pool)
{
    QVector<ChunkSection> mixed;
    QVector<ChunkSection> uniform;
    for (const ChunkData &chunk : pool) {
        for (const ChunkSection &s : chunk.sections) {
            (s.uniform ? uniform : mixed).append(s);
        }
    }
    int paletteTotal = 0;
    for (const ChunkSection &s : std::as_const(mixed)) {
        paletteTotal += s.palette.size();
    }
    std::printf("\ncodec per section, one thread, before -> after (%lld non-uniform sections, mean palette %.1f; "
                "%lld uniform):\n",
                static_cast<long long>(mixed.size()), double(paletteTotal) / mixed.size(),
                static_cast<long long>(uniform.size()));

    // Each stage's results are dropped as it goes, as they are in a listing: keeping thousands
    // of fresh 8 KB index arrays alive would time page faults rather than the codec.
    auto stage = [](const QVector<ChunkSection> &sections, const char *label) {
        quint64 sink = 0;
        auto perSection = [&sections](auto &&fn) {
            const auto t0 = Clock::now();
            for (const ChunkSection &s : sections) {
                fn(s);
            }
            return secondsSince(t0) * 1e6 / sections.size();
        };
        auto canonOf = [](const ChunkSection &s) { return *SectionCodec::canonicalize(s); };

        const double canonBefore = perSection([&](const ChunkSection &s) {
            sink += referenceCanonicalize(s)->indices.at(100);
        });
        const double canonAfter = perSection([&](const ChunkSection &s) {
            sink += SectionCodec::canonicalize(s)->indices.at(100);
        });
        // canonicalize is part of the timings below; its cost from above is taken back out.
        const double encodeBefore = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(beforeEncodeBlob(canonOf(s)).at(5));
        }) - canonAfter;
        const double encodeAfter = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(SectionCodec::encodeBlob(canonOf(s)).at(5));
        }) - canonAfter;
        const double digestBefore = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(beforeDigest(canonOf(s), kPrefix).at(0));
        }) - canonAfter;
        const double digestAfter = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(SectionCodec::digest(canonOf(s), kPrefix).at(0));
        }) - canonAfter;
        const double listingBefore = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(beforeDigest(*referenceCanonicalize(s), kPrefix).at(0));
        });
        const double listingAfter = perSection([&](const ChunkSection &s) {
            sink += static_cast<quint8>(SectionCodec::sectionDigest(s, kPrefix)->at(0));
        });

        std::printf("  %s (sink %llu)\n", label, static_cast<unsigned long long>(sink % 10));
        std::printf("    canonicalize    %6.2f -> %6.2f us\n", canonBefore, canonAfter);
        // Clamped: a stage cheaper than the timing noise of the canonicalize subtracted from it
        // would otherwise print as negative.
        std::printf("    encodeBlob      %6.2f -> %6.2f us\n", std::max(0.0, encodeBefore), std::max(0.0, encodeAfter));
        std::printf("    digest          %6.2f -> %6.2f us  (before: BLAKE2b over a built blob; after: in place)\n",
                    digestBefore, digestAfter);
        std::printf("    listing total   %6.2f -> %6.2f us  (canonicalize + digest, memoized when uniform)\n",
                    listingBefore, listingAfter);
    };
    stage(mixed, "non-uniform");
    stage(uniform, "uniform");
}

void benchWholeCalls(const QVector<ChunkData> &pool, bool before)
{
    for (int size : {1008, 2000}) {
        auto bot = makeBot(pool, 0, 4096);
        // Warm the tracker so the first timed call is not the since=None full listing.
        markNext(*bot, size);
        bot->token = list(*bot, std::nullopt, kNether, false).token;
        double total = 0;
        qint64 listed = 0;
        const int iterations = 200;
        for (int i = 0; i < iterations; ++i) {
            markNext(*bot, size);
            const auto t0 = Clock::now();
            const SectionObservation::Changes changes =
                before ? listBefore(*bot, bot->token, true) : list(*bot, bot->token);
            total += secondsSince(t0);
            listed += changes.sections.size();
            bot->token = changes.token;
        }
        std::printf("  %s %4lld sections/call: %8.2f ms/call  %9.0f sections/s\n", before ? "before" : "after ",
                    static_cast<long long>(listed / iterations), total * 1e3 / iterations, listed / total);
    }
}

// A poll scans the whole dropped log under the tracker mutex, which chunk loads on the main
// thread wait on: time one against a full log with nothing new in it.
void benchDroppedLog(const QVector<ChunkData> &pool)
{
    Bot bot;
    for (int c = 0; c < 32768 / 16; ++c) {
        loadColumn(bot, pool[c % pool.size()], c, 0);
        unloadColumn(bot, c, 0);
    }
    const quint64 token = list(bot, std::nullopt).token;
    const int polls = 2000;
    const auto t0 = Clock::now();
    qsizetype sink = 0;
    for (int i = 0; i < polls; ++i) {
        sink += list(bot, token).droppedTotal;
    }
    std::printf("\nempty poll against a full dropped log (32768 entries): %.1f us (sink %lld)\n",
                secondsSince(t0) * 1e6 / polls, static_cast<long long>(sink));
}

// `callers` callers, one bot each, while a writer per bot keeps reloading columns under the
// write lock at the rate a flying bot loads them - the contention the main thread adds.
void benchConcurrent(const QVector<ChunkData> &pool, int callers, double seconds, bool exporting, bool before)
{
    std::vector<std::unique_ptr<Bot>> bots;
    for (int i = 0; i < callers; ++i) {
        bots.push_back(makeBot(pool, i, 4096));
        markNext(*bots.back(), 16);
        bots.back()->token = list(*bots.back(), std::nullopt, kNether, false).token;
    }

    std::atomic<bool> stop{false};
    std::vector<qint64> done(callers, 0);
    std::vector<qint64> calls(callers, 0);
    std::vector<std::thread> threads;
    std::vector<std::thread> writers;
    for (int i = 0; i < callers; ++i) {
        writers.emplace_back([&, i] {
            Bot &bot = *bots[i];
            int n = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                const SectionColumnKey col = bot.columns[(n * 31) % bot.columns.size()];
                ChunkData chunk = pool[n % pool.size()];
                chunk.chunkX = col.chunkX;
                chunk.chunkZ = col.chunkZ;
                {
                    QWriteLocker locker(&bot.lock);
                    bot.world.loadChunk(chunk);
                }
                ++n;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    const auto start = Clock::now();
    for (int i = 0; i < callers; ++i) {
        threads.emplace_back([&, i] {
            Bot &bot = *bots[i];
            while (!stop.load(std::memory_order_relaxed)) {
                if (exporting) {
                    // What an uploader asks for on new ground: the nine content sections of each
                    // column, 256 keys a call.
                    QVector<SectionKey> keys;
                    for (int c = 0; c < 29; ++c) {
                        const SectionColumnKey col = bot.columns[bot.nextColumn];
                        bot.nextColumn = (bot.nextColumn + 1) % bot.columns.size();
                        for (int sy = 0; sy < 9; ++sy) {
                            keys.append({col.chunkX, col.chunkZ, sy});
                        }
                    }
                    const QByteArray payload = before
                        ? exportBefore(bot, keys)
                        : SectionObservation::exportSections(bot.world, bot.lock, keys, kNether);
                    done[i] += payload.isEmpty() ? 0 : keys.size();
                } else {
                    markNext(bot, 1504);
                    const SectionObservation::Changes changes =
                        before ? listBefore(bot, bot.token, true) : list(bot, bot.token);
                    done[i] += changes.sections.size();
                    bot.token = changes.token;
                }
                ++calls[i];
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    stop = true;
    for (std::thread &t : threads) {
        t.join();
    }
    const double elapsed = secondsSince(start);
    for (std::thread &t : writers) {
        t.join();
    }

    qint64 total = 0;
    qint64 totalCalls = 0;
    for (int i = 0; i < callers; ++i) {
        total += done[i];
        totalCalls += calls[i];
    }
    std::printf("  %s %2d C++ callers:    %9.0f sections/s total, %6.2f ms/call\n", before ? "before" : "after ",
                callers, total / elapsed, elapsed * 1e3 * callers / std::max<qint64>(1, totalCalls));
}

// Python layer. The module mirrors the production world bindings for the result types and
// PythonAPI::changedSections' GIL handling, so ten Python threads here pay what ten threads
// in a script pay.
std::vector<std::unique_ptr<Bot>> *g_pyBots = nullptr;

PySectionChanges toPy(SectionObservation::Changes &&changes)
{
    PySectionChanges result;
    result.token = changes.token;
    result.truncated = changes.truncated;
    result.droppedTotal = static_cast<size_t>(changes.droppedTotal);
    result.droppedIncomplete = changes.droppedIncomplete;
    for (const SectionKey &key : std::as_const(changes.dropped)) {
        result.dropped.emplace_back(key.chunkX, key.chunkZ, key.sectionY);
    }
    result.sections.reserve(changes.sections.size());
    for (const SectionObservation::Change &c : std::as_const(changes.sections)) {
        PySectionChange change;
        change.chunkX = c.key.chunkX;
        change.chunkZ = c.key.chunkZ;
        change.sectionY = c.key.sectionY;
        if (!c.digest.isEmpty()) {
            change.digestBytes.assign(c.digest.constData(), static_cast<size_t>(c.digest.size()));
            change.hasDigest = true;
        }
        result.sections.push_back(std::move(change));
    }
    return result;
}

}

PYBIND11_EMBEDDED_MODULE(section_bench, m) {
    py::class_<PySectionChange>(m, "SectionChange")
        .def_readonly("chunk_x", &PySectionChange::chunkX)
        .def_readonly("chunk_z", &PySectionChange::chunkZ)
        .def_readonly("section_y", &PySectionChange::sectionY)
        .def_property_readonly("digest", [](const PySectionChange &c) -> py::object {
            if (!c.hasDigest) return py::none();
            return py::bytes(c.digestBytes);
        })
        .def_property_readonly("key", [](const PySectionChange &c) {
            return py::make_tuple(c.chunkX, c.chunkZ, c.sectionY);
        });

    py::class_<PySectionChanges>(m, "SectionChanges")
        .def_readonly("token", &PySectionChanges::token)
        .def_readonly("truncated", &PySectionChanges::truncated)
        .def_readonly("sections", &PySectionChanges::sections)
        .def_readonly("dropped", &PySectionChanges::dropped)
        .def_readonly("dropped_total", &PySectionChanges::droppedTotal)
        .def_readonly("dropped_incomplete", &PySectionChanges::droppedIncomplete);

    m.def("mark", [](int bot, int sections) {
        py::gil_scoped_release release;
        return markNext(*(*g_pyBots)[bot], sections);
    });

    m.def("changed_sections", [](int bot, const py::object &since, const std::string &dimension, bool digest,
                                 int limit, const py::bytes &digestPrefix, bool before) {
        std::optional<quint64> sinceSeq;
        if (!since.is_none()) {
            sinceSeq = since.cast<quint64>();
        }
        const QByteArray dimensionFilter = QByteArray::fromStdString(dimension);
        const QByteArray prefix = QByteArray::fromStdString(digestPrefix.cast<std::string>());
        Bot &b = *(*g_pyBots)[bot];
        py::gil_scoped_release release;
        if (before) {
            return toPy(listBefore(b, sinceSeq, digest));
        }
        return toPy(SectionObservation::listChanges(b.tracker, b.world, b.lock, sinceSeq, dimensionFilter, digest,
                                                    limit, prefix));
    });
}

namespace {

void benchPython(const QVector<ChunkData> &pool, double seconds, bool before)
{
    std::vector<std::unique_ptr<Bot>> bots;
    for (int i = 0; i < 10; ++i) {
        bots.push_back(makeBot(pool, i, 4096));
    }
    g_pyBots = &bots;

    py::dict scope;
    scope["seconds"] = seconds;
    scope["before"] = before;
    py::exec(R"(
import threading, time
import section_bench as sb

PREFIX = b"hiveobs1\0"

def run(threads):
    out = {}
    def worker(i, stop_at):
        tok = sb.changed_sections(i, None, "minecraft:the_nether", False, 0, PREFIX, before).token
        n = calls = 0
        py_s = 0.0
        while time.perf_counter() < stop_at:
            sb.mark(i, 1504)
            ch = sb.changed_sections(i, tok, "minecraft:the_nether", True, 0, PREFIX, before)
            t0 = time.perf_counter()
            if ch.sections:
                ch.sections[0].digest
            rows = [(c.chunk_x, c.chunk_z, c.section_y, c.digest) for c in ch.sections]
            py_s += time.perf_counter() - t0
            tok = ch.token
            n += len(rows)
            calls += 1
        out[i] = (n, calls, py_s)
    stop_at = time.perf_counter() + seconds
    ts = [threading.Thread(target=worker, args=(i, stop_at)) for i in range(threads)]
    start = time.perf_counter()
    for t in ts: t.start()
    for t in ts: t.join()
    elapsed = time.perf_counter() - start
    n = sum(v[0] for v in out.values())
    calls = sum(v[1] for v in out.values())
    py_s = sum(v[2] for v in out.values())
    return n / elapsed, elapsed * 1e3 * threads / max(1, calls), py_s * 1e6 / max(1, n)

results = [(t,) + run(t) for t in (1, 10)]
)", scope);

    for (const py::handle row : scope["results"]) {
        const auto t = row.cast<py::tuple>();
        std::printf("  %s %2d Python threads: %9.0f sections/s total, %6.2f ms/call, %.2f us/section in Python\n",
                    before ? "before" : "after ", t[0].cast<int>(), t[1].cast<double>(), t[2].cast<double>(), t[3].cast<double>());
    }
    g_pyBots = nullptr;
}

}

int main(int argc, char **argv)
{
    bool checksOnly = false;
    double seconds = 3.0;
    const char *only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--checks-only") == 0) {
            checksOnly = true;
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        }
    }
    auto wanted = [only](const char *name) { return !only || std::strcmp(only, name) == 0; };

    std::printf("building a pool of 1024 nether columns...\n");
    const QVector<ChunkData> pool = makePool(1024);

    checkKnownAnswers();
    checkMemo();
    checkEquivalence(pool);
    checkExport(pool);
    checkDropped(pool);
    checkNoSilentDrops(pool);
    if (g_failures > 0) {
        std::printf("\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    if (checksOnly) {
        return 0;
    }

    std::printf("\n%d hardware threads\n", QThread::idealThreadCount());
    if (wanted("codec")) {
        benchCodec(pool);
    }
    if (wanted("calls")) {
        std::printf("\nwhole listing calls, digest=True, one caller:\n");
        for (bool before : {true, false}) {
            benchWholeCalls(pool, before);
        }
        benchDroppedLog(pool);
    }
    if (wanted("listing")) {
        std::printf("\nconcurrent listing callers, 1504 sections/call, digest=True, %.0f s each:\n", seconds);
        for (bool before : {true, false}) {
            benchConcurrent(pool, 1, seconds, false, before);
            benchConcurrent(pool, 10, seconds, false, before);
        }
    }
    if (wanted("python")) {
        std::printf("\nconcurrent Python callers, 1504 sections/call, digest=True, %.0f s each:\n", seconds);
        // The interpreter may warn on stderr as it starts; keep that off the middle of a line.
        std::fflush(stdout);
        py::scoped_interpreter interpreter;
        for (bool before : {true, false}) {
            benchPython(pool, seconds, before);
        }
    }
    if (wanted("export")) {
        std::printf("\nconcurrent export callers, 261 content sections/call, %.0f s each:\n", seconds);
        for (bool before : {true, false}) {
            benchConcurrent(pool, 1, seconds, true, before);
            benchConcurrent(pool, 10, seconds, true, before);
        }
    }
    return 0;
}
