#ifndef RETAINEDEXTRAS_H
#define RETAINEDEXTRAS_H

#include "bot/RetainedSections.h"
#include "bot/SectionDirtyTracker.h"
#include "world/ColumnExtrasCodec.h"

#include <QByteArray>
#include <QHash>
#include <deque>

// RetainedSections' counterpart for world.changed_extras: the extras of columns that unloaded
// while still marked, so a poll can list and export them after the column is gone. Block
// entities leave memory with their column, so unlike RetainedSections this keeps the
// canonical form, made at unload - a few hundred bytes a column, where keeping the sections
// alive would hold ~256 KB each.
//
// Guarded by the bot's world lock, like RetainedSections.
class RetainedExtras
{
public:
    static constexpr qsizetype kMaxColumns = 16384;
    static constexpr qint64 kMaxAgeMs = RetainedSections::kMaxAgeMs;

    struct Entry {
        quint64 seq = 0;
        QByteArray dimension;
        ColumnExtrasCodec::CanonicalColumn column;
        qint64 at = 0;
        quint64 stamp = 0;
    };

    void retain(const SectionColumnKey &key, quint64 seq, const QByteArray &dimension,
                ColumnExtrasCodec::CanonicalColumn column, qint64 now)
    {
        expire(now);
        ++stamps;
        byKey.insert(key, Entry{seq, dimension, std::move(column), now, stamps});
        order.push_back({key, now, stamps});
        while (byKey.size() > maxColumns && !order.empty()) {
            evictFront();
        }
    }

    const Entry *latest(const SectionColumnKey &key, qint64 now) const
    {
        auto it = byKey.constFind(key);
        if (it == byKey.cend() || now - it->at > kMaxAgeMs) {
            return nullptr;
        }
        return &*it;
    }

    qsizetype size() const { return byKey.size(); }

    void clear()
    {
        byKey.clear();
        order.clear();
    }

    qsizetype maxColumns = kMaxColumns;  // the bench lowers it

private:
    struct Order {
        SectionColumnKey key;
        qint64 at;
        quint64 stamp;
    };

    void expire(qint64 now)
    {
        while (!order.empty() && now - order.front().at > kMaxAgeMs) {
            evictFront();
        }
    }

    void evictFront()
    {
        const Order front = order.front();
        order.pop_front();
        auto it = byKey.find(front.key);
        if (it != byKey.end() && it->stamp == front.stamp) {
            byKey.erase(it);
        }
    }

    QHash<SectionColumnKey, Entry> byKey;
    std::deque<Order> order;
    quint64 stamps = 0;
};

#endif // RETAINEDEXTRAS_H
