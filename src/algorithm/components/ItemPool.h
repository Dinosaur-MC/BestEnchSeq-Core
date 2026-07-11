#pragma once
#include "types/CompactedTypes.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

/// Append-only item pool with hash-based deduplication.
///
/// Each unique Item (type + ppn + dur + enchs) gets a stable ItemID.
/// Identical items added multiple times return the same ID.
class ItemPool {
public:
    using ItemID = int32_t;
    static constexpr ItemID INVALID_ITEM_ID = -1;

    void set_max(size_t n) noexcept { _max_items = n; }

    /// Add an item, deduplicating by content. Returns INVALID_ITEM_ID when full.
    ItemID add(compact::Item item) {
        auto it = _dedup.find(item);
        if (it != _dedup.end())
            return it->second;
        if (_items.size() >= _max_items) return INVALID_ITEM_ID;
        ItemID id = static_cast<ItemID>(_items.size());
        _items.push_back(std::move(item));
        _dedup[_items.back()] = id;
        return id;
    }

    const compact::Item& operator[](ItemID id) const noexcept {
        return _items[static_cast<size_t>(id)];
    }

    compact::Item& operator[](ItemID id) noexcept {
        return _items[static_cast<size_t>(id)];
    }

    size_t size()     const noexcept { return _items.size(); }
    size_t capacity() const noexcept { return _items.capacity(); }
    void reserve(size_t n) { _items.reserve(n); _dedup.reserve(n); }
    void clear() { _items.clear(); _dedup.clear(); }

private:
    std::vector<compact::Item> _items;
    std::unordered_map<compact::Item, ItemID> _dedup;
    size_t _max_items{10'000'000};
};
