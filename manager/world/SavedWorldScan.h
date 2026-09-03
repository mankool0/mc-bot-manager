#pragma once

#include "bot/WorldData.h"
#include <QSet>
#include <QString>
#include <tag_compound.h>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Read-only search over the manager's saved world: the .mca region files an autosave leaves
// behind. A scan covers a horizontal disc, opens every region file inside it once (regions are
// spread over worker threads), decodes only the parts of each chunk the query needs and never
// touches the chunks in `skip` - the caller's in-memory ones, which it searches itself.
namespace SavedWorldScan {

// A sphere of block positions plus the block ids (state suffix already stripped) to look for.
struct BlockQuery {
    std::vector<std::string> ids;
    double centerX = 0, centerY = 0, centerZ = 0;
    double radiusSq = 0;
    bool inclusive = true;  // a block exactly radius away counts
    int minY = -64, maxY = 320;  // inclusive block y bounds
    int minBlockLight = 0, maxBlockLight = 15, minSkyLight = 0, maxSkyLight = 15;

    bool filtersLight() const;
    bool matchesId(std::string_view id) const;
    bool within(double distSq) const { return inclusive ? distSq <= radiusSq : distSq < radiusSq; }
    double radius() const { return std::sqrt(radiusSq); }
};

struct BlockHit {
    int x = 0, y = 0, z = 0;
    double distSq = 0;
};

struct BlockEntityHit {
    std::string type;
    int x = 0, y = 0, z = 0;
    double distSq = 0;  // horizontal, from the disc centre
    std::shared_ptr<const nbt::tag_compound> nbt;  // the block entity compound, owned
};

// The block id of a state string: "minecraft:chest[facing=north]" -> "minecraft:chest".
std::string stripState(const std::string& blockState);

// Region directory of `dimension` in the save, or empty when the save has none for it.
QString regionDir(const QString& worldPath, int dataVersion, const QString& dimension);

// Whether the chunk's 16x16 footprint reaches into the disc.
bool chunkInDisc(int chunkX, int chunkZ, double centerX, double centerZ, double radiusSq);

// Blocks matching `query` in saved chunks, the chunks in `skip` excluded, nearest first.
std::vector<BlockHit> findBlocks(const QString& regionDir, const BlockQuery& query, const QSet<ChunkPos>& skip);

// Nearest such block to the query centre, strictly inside `query.radiusSq`.
std::optional<BlockHit> findNearestBlock(const QString& regionDir, const BlockQuery& query, const QSet<ChunkPos>& skip);

// Block entities whose id is in `types` (all when empty) within `radius` of the centre, any y,
// nearest first.
std::vector<BlockEntityHit> findBlockEntities(const QString& regionDir, const std::vector<std::string>& types,
                                              double centerX, double centerZ, double radius,
                                              const QSet<ChunkPos>& skip);

// The same block matching over an in-memory chunk.
void matchChunk(const ChunkData& chunk, const BlockQuery& query, std::vector<BlockHit>& out);

}
