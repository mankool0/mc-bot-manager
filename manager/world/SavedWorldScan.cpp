#include "SavedWorldScan.h"
#include "NBTSerializer.h"
#include "RegionFile.h"
#include "WorldExporter.h"
#include <QDir>
#include <QtEndian>
#include <io/stream_reader.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>

namespace SavedWorldScan {

namespace {

constexpr size_t npos = SIZE_MAX;

// Cursor over a decompressed chunk's NBT bytes: walks the tree without building it, so a scan
// pays only for the parts it decodes. Every read is bounds-checked; malformed data ends the walk.
class NbtWalker {
public:
    enum Type : uint8_t {
        End = 0, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray
    };

    NbtWalker(const uint8_t* data, size_t size) : d(data), n(size) {}

    // Payload offset of the root compound, or npos.
    size_t root() const {
        if (n < 3 || d[0] != Compound) return npos;
        return advance(3, u16(1));
    }

    // Calls fn(type, name, payloadPos) for each child of the compound whose payload starts at
    // `pos`. Returns the offset past the End tag, or npos.
    template <typename Fn>
    size_t forEachChild(size_t pos, Fn&& fn) const {
        while (pos < n) {
            uint8_t type = d[pos++];
            if (type == End) return pos;
            if (pos + 2 > n) return npos;
            uint16_t nameLen = u16(pos);
            pos += 2;
            if (nameLen > n - pos) return npos;
            std::string_view name(reinterpret_cast<const char*>(d + pos), nameLen);
            pos += nameLen;
            fn(type, name, pos);
            pos = skip(type, pos);
            if (pos == npos) return npos;
        }
        return npos;
    }

    // Calls fn(elemType, payloadPos) for each element of the list whose payload starts at `pos`.
    template <typename Fn>
    size_t forEachElement(size_t pos, Fn&& fn) const {
        if (pos + 5 > n) return npos;
        uint8_t elemType = d[pos];
        int32_t count = i32(pos + 1);
        if (count < 0) return npos;
        pos += 5;
        for (int32_t i = 0; i < count; ++i) {
            fn(elemType, pos);
            pos = skip(elemType, pos);
            if (pos == npos) return npos;
        }
        return pos;
    }

    // Offset past the payload of `type` at `pos`, or npos.
    size_t skip(uint8_t type, size_t pos) const {
        switch (type) {
        case Byte: return advance(pos, 1);
        case Short: return advance(pos, 2);
        case Int: return advance(pos, 4);
        case Long: return advance(pos, 8);
        case Float: return advance(pos, 4);
        case Double: return advance(pos, 8);
        case ByteArray: return skipArray(pos, 1);
        case String: {
            if (pos + 2 > n) return npos;
            return advance(pos + 2, u16(pos));
        }
        case List: {
            if (pos + 5 > n) return npos;
            uint8_t elemType = d[pos];
            int32_t count = i32(pos + 1);
            if (count < 0) return npos;
            pos += 5;
            size_t fixed = fixedSize(elemType);
            if (fixed) return advance(pos, static_cast<uint64_t>(count) * fixed);
            if (elemType == End) return count == 0 ? pos : npos;
            for (int32_t i = 0; i < count; ++i) {
                pos = skip(elemType, pos);
                if (pos == npos) return npos;
            }
            return pos;
        }
        case Compound: return forEachChild(pos, [](uint8_t, std::string_view, size_t) {});
        case IntArray: return skipArray(pos, 4);
        case LongArray: return skipArray(pos, 8);
        default: return npos;
        }
    }

    int8_t i8(size_t p) const { return static_cast<int8_t>(d[p]); }
    uint16_t u16(size_t p) const { return qFromBigEndian<quint16>(d + p); }
    int32_t i32(size_t p) const { return qFromBigEndian<qint32>(d + p); }
    int64_t i64(size_t p) const { return qFromBigEndian<qint64>(d + p); }

    // A String payload at `p`; empty when it runs past the end.
    std::string_view str(size_t p) const {
        if (p + 2 > n) return {};
        uint16_t len = u16(p);
        if (len > n - p - 2) return {};
        return std::string_view(reinterpret_cast<const char*>(d + p + 2), len);
    }

    // Element count of an array payload at `p` whose elements are `elemSize` bytes, or -1 when
    // the array runs past the end. Elements start at p + 4.
    int32_t arrayLength(size_t p, size_t elemSize) const {
        if (p + 4 > n) return -1;
        int32_t count = i32(p);
        if (count < 0 || static_cast<uint64_t>(count) * elemSize > n - p - 4) return -1;
        return count;
    }

    const uint8_t* data() const { return d; }

private:
    size_t advance(size_t pos, uint64_t by) const {
        if (pos > n || by > n - pos) return npos;
        return pos + by;
    }

    size_t skipArray(size_t pos, size_t elemSize) const {
        int32_t count = arrayLength(pos, elemSize);
        if (count < 0) return npos;
        return pos + 4 + static_cast<size_t>(count) * elemSize;
    }

    static size_t fixedSize(uint8_t type) {
        switch (type) {
        case Byte: return 1;
        case Short: return 2;
        case Int: return 4;
        case Long: return 8;
        case Float: return 4;
        case Double: return 8;
        default: return 0;
        }
    }

    const uint8_t* d;
    size_t n;
};

// Squared distance from the centre to the nearest point of the block range [minX, maxX] x
// [minZ, maxZ] (block coordinates, so a lower bound on any block's distance in it).
double footprintDistSq(int minX, int maxX, int minZ, int maxZ, double centerX, double centerZ)
{
    double dx = std::max({minX - centerX, 0.0, centerX - maxX});
    double dz = std::max({minZ - centerZ, 0.0, centerZ - maxZ});
    return dx * dx + dz * dz;
}

double chunkDistSq(int chunkX, int chunkZ, double centerX, double centerZ)
{
    return footprintDistSq(chunkX * 16, chunkX * 16 + 15, chunkZ * 16, chunkZ * 16 + 15, centerX, centerZ);
}

double regionDistSq(int regionX, int regionZ, double centerX, double centerZ)
{
    return footprintDistSq(regionX * 512, regionX * 512 + 511, regionZ * 512, regionZ * 512 + 511, centerX, centerZ);
}

struct RegionRef {
    int x = 0, z = 0;
    double distSq = 0;
    QString path;
};

// Region files of `regionDir` whose 512x512 footprint reaches the disc, nearest first.
std::vector<RegionRef> regionsInDisc(const QString& regionDir, double centerX, double centerZ, double radius)
{
    std::vector<RegionRef> regions;
    if (regionDir.isEmpty() || radius < 0) return regions;

    double radiusSq = radius * radius;
    QDir dir(regionDir);
    const QStringList names = dir.entryList({QStringLiteral("r.*.*.mca")}, QDir::Files);
    for (const QString& name : names) {
        const QStringList parts = name.split('.');
        if (parts.size() != 4) continue;
        bool okX = false, okZ = false;
        int rx = parts[1].toInt(&okX);
        int rz = parts[2].toInt(&okZ);
        if (!okX || !okZ) continue;
        double distSq = regionDistSq(rx, rz, centerX, centerZ);
        if (distSq > radiusSq) continue;
        regions.push_back({rx, rz, distSq, dir.filePath(name)});
    }
    std::sort(regions.begin(), regions.end(), [](const RegionRef& a, const RegionRef& b) {
        return a.distSq < b.distSq;
    });
    return regions;
}

// Regions are handed to this many threads at most; each worker owns one open region file and
// its own output, so a visitor only ever touches the state for its `worker` index.
constexpr int kMaxWorkers = 16;

struct DiscScan {
    QString regionDir;
    double centerX = 0, centerZ = 0, radius = 0;
    const QSet<ChunkPos>* skip = nullptr;
    // Optional prune: false for a footprint at this squared distance means "not worth reading".
    std::function<bool(double minDistSq)> stillWanted;
};

// Runs `visit(worker, chunkX, chunkZ, nbtBytes)` for every saved chunk inside the disc that is
// not in `skip`, opening each region file once. Visitors run concurrently.
template <typename Visit>
void scanDisc(const DiscScan& scan, Visit&& visit)
{
    std::vector<RegionRef> regions = regionsInDisc(scan.regionDir, scan.centerX, scan.centerZ, scan.radius);
    if (regions.empty()) return;

    double radiusSq = scan.radius * scan.radius;
    std::atomic<size_t> next{0};

    auto work = [&](int worker) {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= regions.size()) break;
            const RegionRef& region = regions[i];
            if (scan.stillWanted && !scan.stillWanted(region.distSq)) continue;

            RegionFile file(region.path, RegionFile::Mode::ReadOnly);
            if (!file.isValid()) continue;

            for (int lz = 0; lz < 32; ++lz) {
                for (int lx = 0; lx < 32; ++lx) {
                    if (!file.hasChunk(lx, lz)) continue;
                    int cx = region.x * 32 + lx;
                    int cz = region.z * 32 + lz;
                    double distSq = chunkDistSq(cx, cz, scan.centerX, scan.centerZ);
                    if (distSq > radiusSq) continue;
                    if (scan.skip && scan.skip->contains(ChunkPos(cx, cz))) continue;
                    if (scan.stillWanted && !scan.stillWanted(distSq)) continue;

                    std::vector<uint8_t> bytes = file.readChunkRaw(lx, lz);
                    if (bytes.empty()) continue;
                    try {
                        visit(worker, cx, cz, bytes);
                    } catch (...) {
                        // A chunk that fails to decode contributes nothing.
                    }
                }
            }
        }
    };

    size_t hardware = std::max(1u, std::thread::hardware_concurrency());
    int workers = static_cast<int>(std::min({regions.size(), hardware, static_cast<size_t>(kMaxWorkers)}));
    if (workers <= 1) {
        work(0);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back(work, w);
    }
    for (std::thread& t : threads) {
        t.join();
    }
}

int nibble(const uint8_t* arr, int idx)
{
    return (arr[idx >> 1] >> ((idx & 1) * 4)) & 0xF;
}

// Emits the blocks of one 16x16x16 section that match: `match[i]` says whether palette entry
// i is wanted, `indexAt(i)` is the palette index of block i (YZX order), `lightAt(i)` its light.
template <typename IndexAt, typename LightAt>
void emitSectionHits(int chunkX, int sectionY, int chunkZ, const std::vector<char>& match,
                     IndexAt&& indexAt, LightAt&& lightAt, const BlockQuery& q, std::vector<BlockHit>& out)
{
    int baseX = chunkX * 16;
    int baseY = sectionY * 16;
    int baseZ = chunkZ * 16;
    bool light = q.filtersLight();
    for (int i = 0; i < 4096; ++i) {
        uint32_t idx = indexAt(i);
        if (idx >= match.size() || !match[idx]) continue;
        int y = baseY + (i >> 8);
        if (y < q.minY || y > q.maxY) continue;
        int x = baseX + (i & 15);
        int z = baseZ + ((i >> 4) & 15);
        double dx = x - q.centerX;
        double dy = y - q.centerY;
        double dz = z - q.centerZ;
        double distSq = dx * dx + dy * dy + dz * dz;
        if (!q.within(distSq)) continue;
        if (light) {
            ChunkSection::LightLevels l = lightAt(i);
            if (l.block < q.minBlockLight || l.block > q.maxBlockLight ||
                l.sky < q.minSkyLight || l.sky > q.maxSkyLight) continue;
        }
        out.push_back({x, y, z, distSq});
    }
}

bool sectionInRange(int sectionY, const BlockQuery& q)
{
    return sectionY * 16 + 15 >= q.minY && sectionY * 16 <= q.maxY;
}

// One `sections` element of a saved chunk. Only sections whose palette holds a wanted id have
// their packed indices decoded.
void scanSavedSection(const NbtWalker& w, size_t pos, int chunkX, int chunkZ, const BlockQuery& q,
                      std::vector<BlockHit>& out)
{
    std::optional<int> sectionY;
    size_t blockStates = npos, blockLight = npos, skyLight = npos;
    w.forEachChild(pos, [&](uint8_t type, std::string_view name, size_t p) {
        if (name == "Y" && type == NbtWalker::Byte) sectionY = w.i8(p);
        else if (name == "block_states" && type == NbtWalker::Compound) blockStates = p;
        else if (name == "BlockLight" && type == NbtWalker::ByteArray) blockLight = p;
        else if (name == "SkyLight" && type == NbtWalker::ByteArray) skyLight = p;
    });
    if (!sectionY || blockStates == npos || !sectionInRange(*sectionY, q)) return;

    size_t palette = npos, data = npos;
    w.forEachChild(blockStates, [&](uint8_t type, std::string_view name, size_t p) {
        if (name == "palette" && type == NbtWalker::List) palette = p;
        else if (name == "data" && type == NbtWalker::LongArray) data = p;
    });
    if (palette == npos) return;

    std::vector<char> match;
    w.forEachElement(palette, [&](uint8_t elemType, size_t p) {
        bool wanted = false;
        if (elemType == NbtWalker::Compound) {
            w.forEachChild(p, [&](uint8_t type, std::string_view name, size_t pp) {
                if (name == "Name" && type == NbtWalker::String) wanted = q.matchesId(w.str(pp));
            });
        }
        match.push_back(wanted);
    });
    if (std::none_of(match.begin(), match.end(), [](char m) { return m; })) return;

    const uint8_t* blockLightArr = blockLight != npos && w.arrayLength(blockLight, 1) == 2048 ? w.data() + blockLight + 4 : nullptr;
    const uint8_t* skyLightArr = skyLight != npos && w.arrayLength(skyLight, 1) == 2048 ? w.data() + skyLight + 4 : nullptr;
    auto lightAt = [&](int i) {
        ChunkSection::LightLevels l;
        if (blockLightArr) l.block = nibble(blockLightArr, i);
        if (skyLightArr) l.sky = nibble(skyLightArr, i);
        return l;
    };

    if (data == npos) {
        // No data array: the whole section is palette[0].
        emitSectionHits(chunkX, *sectionY, chunkZ, match, [](int) { return 0u; }, lightAt, q, out);
        return;
    }

    int32_t longCount = w.arrayLength(data, 8);
    if (longCount < 0) return;
    const uint8_t* longs = w.data() + data + 4;
    int bits = NBTSerializer::blockStateBitsPerEntry(static_cast<int>(match.size()));
    int perLong = 64 / bits;
    uint64_t mask = (1ULL << bits) - 1;
    auto indexAt = [&](int i) -> uint32_t {
        int li = i / perLong;
        if (li >= longCount) return 0;
        uint64_t v = static_cast<uint64_t>(qFromBigEndian<qint64>(longs + li * 8));
        return static_cast<uint32_t>((v >> ((i % perLong) * bits)) & mask);
    };
    emitSectionHits(chunkX, *sectionY, chunkZ, match, indexAt, lightAt, q, out);
}

void scanSavedChunkBlocks(const std::vector<uint8_t>& bytes, int chunkX, int chunkZ, const BlockQuery& q,
                          std::vector<BlockHit>& out)
{
    NbtWalker w(bytes.data(), bytes.size());
    size_t root = w.root();
    if (root == npos) return;
    size_t sections = npos;
    w.forEachChild(root, [&](uint8_t type, std::string_view name, size_t p) {
        if (name == "sections" && type == NbtWalker::List) sections = p;
    });
    if (sections == npos) return;
    w.forEachElement(sections, [&](uint8_t elemType, size_t p) {
        if (elemType == NbtWalker::Compound) scanSavedSection(w, p, chunkX, chunkZ, q, out);
    });
}

// Builds one compound from its NBT bytes with libnbt++ (the only place a scan builds a tree).
std::shared_ptr<const nbt::tag_compound> parseCompound(const uint8_t* bytes, size_t size)
{
    try {
        std::istringstream iss(std::string(reinterpret_cast<const char*>(bytes), size), std::ios::binary);
        nbt::io::stream_reader reader(iss);
        std::unique_ptr<nbt::tag> tag = reader.read_payload(nbt::tag_type::Compound);
        auto* compound = dynamic_cast<nbt::tag_compound*>(tag.get());
        if (!compound) return nullptr;
        tag.release();
        return std::shared_ptr<const nbt::tag_compound>(compound);
    } catch (...) {
        return nullptr;
    }
}

void scanSavedChunkBlockEntities(const std::vector<uint8_t>& bytes, const std::vector<std::string>& types,
                                 double centerX, double centerZ, double radiusSq, std::vector<BlockEntityHit>& out)
{
    NbtWalker w(bytes.data(), bytes.size());
    size_t root = w.root();
    if (root == npos) return;
    size_t list = npos;
    w.forEachChild(root, [&](uint8_t type, std::string_view name, size_t p) {
        if (name == "block_entities" && type == NbtWalker::List) list = p;
    });
    if (list == npos) return;

    w.forEachElement(list, [&](uint8_t elemType, size_t p) {
        if (elemType != NbtWalker::Compound) return;
        std::string_view id;
        int x = 0, y = 0, z = 0;
        w.forEachChild(p, [&](uint8_t type, std::string_view name, size_t pp) {
            if (name == "id" && type == NbtWalker::String) id = w.str(pp);
            else if (name == "x" && type == NbtWalker::Int) x = w.i32(pp);
            else if (name == "y" && type == NbtWalker::Int) y = w.i32(pp);
            else if (name == "z" && type == NbtWalker::Int) z = w.i32(pp);
        });
        // The writer leaves an empty compound where it had no NBT; that is not a block entity.
        if (id.empty()) return;
        if (!types.empty() && std::find(types.begin(), types.end(), id) == types.end()) return;
        double dx = x - centerX;
        double dz = z - centerZ;
        double distSq = dx * dx + dz * dz;
        if (distSq > radiusSq) return;
        size_t end = w.skip(NbtWalker::Compound, p);
        if (end == npos) return;
        auto compound = parseCompound(w.data() + p, end - p);
        if (!compound) return;
        out.push_back({std::string(id), x, y, z, distSq, std::move(compound)});
    });
}

// Workers finish in no particular order; nearest first makes the result deterministic.
template <typename Hit>
void sortByDistance(std::vector<Hit>& hits)
{
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.distSq != b.distSq) return a.distSq < b.distSq;
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
}

} // namespace

bool BlockQuery::filtersLight() const
{
    return minBlockLight > 0 || maxBlockLight < 15 || minSkyLight > 0 || maxSkyLight < 15;
}

bool BlockQuery::matchesId(std::string_view id) const
{
    for (const std::string& wanted : ids) {
        if (wanted == id) return true;
    }
    return false;
}

std::string stripState(const std::string& blockState)
{
    size_t bracket = blockState.find('[');
    return bracket == std::string::npos ? blockState : blockState.substr(0, bracket);
}

QString regionDir(const QString& worldPath, int dataVersion, const QString& dimension)
{
    QString dim = dimension.isEmpty() ? QStringLiteral("minecraft:overworld") : dimension;
    if (dim != "minecraft:overworld" && dim != "minecraft:the_nether" && dim != "minecraft:the_end") {
        return {};
    }
    QString dir = WorldExporter::getDimensionPath(worldPath, dim, dataVersion) + "/region";
    return QDir(dir).exists() ? dir : QString();
}

bool chunkInDisc(int chunkX, int chunkZ, double centerX, double centerZ, double radiusSq)
{
    return chunkDistSq(chunkX, chunkZ, centerX, centerZ) <= radiusSq;
}

std::vector<BlockHit> findBlocks(const QString& regionDir, const BlockQuery& query, const QSet<ChunkPos>& skip)
{
    std::vector<std::vector<BlockHit>> perWorker(kMaxWorkers);
    DiscScan scan{regionDir, query.centerX, query.centerZ, query.radius(), &skip, nullptr};
    scanDisc(scan, [&](int worker, int cx, int cz, const std::vector<uint8_t>& bytes) {
        scanSavedChunkBlocks(bytes, cx, cz, query, perWorker[worker]);
    });

    std::vector<BlockHit> hits;
    for (auto& part : perWorker) {
        hits.insert(hits.end(), part.begin(), part.end());
    }
    sortByDistance(hits);
    return hits;
}

std::optional<BlockHit> findNearestBlock(const QString& regionDir, const BlockQuery& query, const QSet<ChunkPos>& skip)
{
    std::mutex mutex;
    std::optional<BlockHit> best;
    double bestSq = query.radiusSq;

    DiscScan scan{regionDir, query.centerX, query.centerZ, query.radius(), &skip,
                  [&](double minDistSq) {
                      std::lock_guard<std::mutex> lock(mutex);
                      return minDistSq < bestSq;
                  }};
    scanDisc(scan, [&](int, int cx, int cz, const std::vector<uint8_t>& bytes) {
        BlockQuery bounded = query;
        bounded.inclusive = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            bounded.radiusSq = bestSq;
        }
        std::vector<BlockHit> hits;
        scanSavedChunkBlocks(bytes, cx, cz, bounded, hits);
        auto nearest = std::min_element(hits.begin(), hits.end(), [](const BlockHit& a, const BlockHit& b) {
            return a.distSq < b.distSq;
        });
        if (nearest == hits.end()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (nearest->distSq < bestSq) {
            bestSq = nearest->distSq;
            best = *nearest;
        }
    });
    return best;
}

std::vector<BlockEntityHit> findBlockEntities(const QString& regionDir, const std::vector<std::string>& types,
                                              double centerX, double centerZ, double radius,
                                              const QSet<ChunkPos>& skip)
{
    std::vector<std::vector<BlockEntityHit>> perWorker(kMaxWorkers);
    double radiusSq = radius * radius;
    DiscScan scan{regionDir, centerX, centerZ, radius, &skip, nullptr};
    scanDisc(scan, [&](int worker, int, int, const std::vector<uint8_t>& bytes) {
        scanSavedChunkBlockEntities(bytes, types, centerX, centerZ, radiusSq, perWorker[worker]);
    });

    std::vector<BlockEntityHit> hits;
    for (auto& part : perWorker) {
        for (auto& hit : part) hits.push_back(std::move(hit));
    }
    sortByDistance(hits);
    return hits;
}

void matchChunk(const ChunkData& chunk, const BlockQuery& query, std::vector<BlockHit>& out)
{
    for (auto it = chunk.sections.cbegin(); it != chunk.sections.cend(); ++it) {
        const ChunkSection& section = it.value();
        if (!sectionInRange(section.sectionY, query)) continue;

        std::vector<char> match;
        match.reserve(section.palette.size());
        for (const QString& state : section.palette) {
            match.push_back(query.matchesId(stripState(state.toStdString())));
        }
        if (std::none_of(match.begin(), match.end(), [](char m) { return m; })) continue;

        const uint8_t* blockLightArr = section.blockLight.size() == 2048 ? reinterpret_cast<const uint8_t*>(section.blockLight.constData()) : nullptr;
        const uint8_t* skyLightArr = section.skyLight.size() == 2048 ? reinterpret_cast<const uint8_t*>(section.skyLight.constData()) : nullptr;
        auto lightAt = [&](int i) {
            ChunkSection::LightLevels l;
            if (blockLightArr) l.block = nibble(blockLightArr, i);
            if (skyLightArr) l.sky = nibble(skyLightArr, i);
            return l;
        };

        if (section.uniform) {
            emitSectionHits(chunk.chunkX, section.sectionY, chunk.chunkZ, match, [](int) { return 0u; }, lightAt, query, out);
            continue;
        }
        if (section.blockIndices.size() != 4096) continue;
        const uint32_t* indices = section.blockIndices.constData();
        emitSectionHits(chunk.chunkX, section.sectionY, chunk.chunkZ, match, [indices](int i) { return indices[i]; }, lightAt, query, out);
    }
}

}
