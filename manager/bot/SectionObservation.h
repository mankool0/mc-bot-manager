#ifndef SECTIONOBSERVATION_H
#define SECTIONOBSERVATION_H

#include "bot/SectionDirtyTracker.h"
#include "world/SectionCodec.h"

#include <QByteArray>
#include <QVector>
#include <optional>

class BotWorldData;
class QReadWriteLock;

// The work behind world.changed_sections / world.export_sections / world.get_section, kept
// free of Python so it runs with the GIL released and can be driven directly by the section
// benchmark. Per-section hashing and encoding is spread over a worker pool; each call reads
// the world under one read lock and never holds it while hashing.
namespace SectionObservation {

// Cap on Changes::dropped; droppedTotal still counts every one.
constexpr int kMaxDroppedKeys = 4096;

struct Change {
    SectionKey key;
    QByteArray digest;  // empty unless a digest was asked for
};

struct Changes {
    quint64 token = 0;
    bool truncated = false;
    QVector<Change> sections;
    // Sections this listing will never return because their content is gone: marked after
    // `since` and unloaded before they could be read (reported only when there is a `since`
    // to be after), or, in practice never, listed but impossible to encode. Deduplicated, and
    // filtered by the dimension where it is known.
    QVector<SectionKey> dropped;
    qsizetype droppedTotal = 0;
    bool droppedIncomplete = false;
};

Changes listChanges(const SectionDirtyTracker &tracker, const BotWorldData &world, QReadWriteLock &worldLock,
                    std::optional<quint64> since, const QByteArray &dimension, bool digest, int limit,
                    const QByteArray &digestPrefix);

QByteArray exportSections(const BotWorldData &world, QReadWriteLock &worldLock, const QVector<SectionKey> &keys,
                          const QByteArray &dimension);

struct Section {
    QByteArray dimension;
    SectionCodec::CanonicalSection canonical;
};

std::optional<Section> readSection(const BotWorldData &world, QReadWriteLock &worldLock, const SectionKey &key,
                                   const QByteArray &dimension);

}

#endif // SECTIONOBSERVATION_H
