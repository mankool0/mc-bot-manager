#pragma once

#include <QHash>
#include <QString>

struct DimChunkPos {
    QString dimension;
    int chunkX = 0, chunkZ = 0;

    bool operator==(const DimChunkPos& other) const {
        return chunkX == other.chunkX && chunkZ == other.chunkZ && dimension == other.dimension;
    }
};

inline size_t qHash(const DimChunkPos& pos, size_t seed = 0) {
    return qHashMulti(seed, pos.dimension, pos.chunkX, pos.chunkZ);
}
