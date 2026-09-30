#ifndef RETAINEDSECTIONS_H
#define RETAINEDSECTIONS_H

#include "bot/SectionDirtyTracker.h"
#include "bot/WorldData.h"

#include <QByteArray>
#include <QHash>
#include <chrono>
#include <deque>

// Sections of unloaded columns, kept so changed_sections can still list them and
// get_section / export_sections still read them. A column can unload before the next poll,
// or between a poll listing a section and the export of it, so every section is kept,
// listed or not.
//
// Bounded by bytes and age: a poller that has not read a section by the
// time its copy is evicted gets it in `dropped`. Entries share data with the unloaded
// column (Qt implicit sharing), so retaining copies nothing.
//
// Guarded by the bot's world lock: written under the write lock on unload, cleared with
// the world, read under the read lock.
class RetainedSections
{
public:
    // ~20 KB a section, so ~13,000 sections: about 13 s of unloads at ~1,000 sections/s.
    static constexpr qsizetype kMaxBytes = 256 * 1024 * 1024;
    static constexpr qint64 kMaxAgeMs = 30000;

    struct Entry {
        quint64 seq = 0;  // the tracker's sequence the section was last marked with
        QByteArray dimension;
        ChunkSection section;
        qint64 at = 0;
        quint64 stamp = 0;  // which retain() wrote it, so a stale `order` entry removes nothing
        qsizetype bytes = 0;
    };

    static qsizetype bytesOf(const ChunkSection &section)
    {
        return static_cast<qsizetype>(sizeof(ChunkSection))
               + section.blockIndices.size() * static_cast<qsizetype>(sizeof(uint32_t))
               + section.biomeIndices.size() * static_cast<qsizetype>(sizeof(uint32_t))
               + section.blockLight.size() + section.skyLight.size()
               + (section.palette.size() + section.biomePalette.size()) * 32;
    }

    static qint64 nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void retain(const SectionKey &key, quint64 seq, const QByteArray &dimension, const ChunkSection &section,
                qint64 now)
    {
        expire(now);
        ++stamps;
        const qsizetype bytes = bytesOf(section);
        auto was = byKey.find(key);
        if (was != byKey.end()) {
            total -= was->bytes;
        }
        byKey.insert(key, Entry{seq, dimension, section, now, stamps, bytes});
        total += bytes;
        order.push_back({key, now, stamps});
        while (total > maxBytes && !order.empty()) {
            evictFront();
        }
    }

    // The newest copy of `key`, or null when there is none or it is past kMaxAgeMs.
    const Entry *latest(const SectionKey &key, qint64 now) const
    {
        auto it = byKey.constFind(key);
        if (it == byKey.cend() || now - it->at > kMaxAgeMs) {
            return nullptr;
        }
        return &*it;
    }

    qsizetype size() const { return byKey.size(); }
    qsizetype bytes() const { return total; }

    void clear()
    {
        byKey.clear();
        order.clear();
        total = 0;
    }

    qsizetype maxBytes = kMaxBytes;  // the bench lowers it

private:
    struct Order {
        SectionKey key;
        qint64 at;
        quint64 stamp;
    };

    void expire(qint64 now)
    {
        while (!order.empty() && now - order.front().at > kMaxAgeMs) {
            evictFront();
        }
    }

    // A key retained again has a newer entry in `order` too; the stale one removes nothing.
    void evictFront()
    {
        const Order front = order.front();
        order.pop_front();
        auto it = byKey.find(front.key);
        if (it != byKey.end() && it->stamp == front.stamp) {
            total -= it->bytes;
            byKey.erase(it);
        }
    }

    QHash<SectionKey, Entry> byKey;
    std::deque<Order> order;
    quint64 stamps = 0;
    qsizetype total = 0;
};

#endif // RETAINEDSECTIONS_H
