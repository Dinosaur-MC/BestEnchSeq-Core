#pragma once
#include "domain/algorithm/types/Item.h"
#include "common/utils/MemoryPool.hpp"
#include <cstdint>
#include <memory_resource>
#include <unordered_map>

namespace algorithm {

/// Append-only item pool with hash-based deduplication.
///
/// Backed by a MemoryPool (pmr monotonic buffer) for allocation.
/// Each unique Item (type + ppn + dur + enchs) gets a stable ItemID.
/// Identical items added multiple times return the same ID.
class ItemPool {
  public:
    using ItemID                            = int32_t;
    static constexpr ItemID INVALID_ITEM_ID = -1;

    void set_max(size_t n) noexcept { _max_items = n; }

    /// Add an item, deduplicating by content.  Returns INVALID_ITEM_ID when full.
    ItemID add(Item item) {
        size_t h = _hash_item(item);
        auto it  = _dedup.find(h);
        if (it != _dedup.end()) {
            // Fast path: hash matched.  Verify content (hash collision guard).
            if (_items[it->second] == item)
                return it->second;
            // 64-bit hash collision between distinct items (~10⁻¹² probability).
            // Fall through and store as a new entry — the collision is so rare
            // that a full-resolution probe would waste more cycles than it saves.
        }
        if (_items.size() >= _max_items)
            return INVALID_ITEM_ID;
        ItemID id = static_cast<ItemID>(_items.size());
        _items.push_back(std::move(item));
        _dedup.emplace(h, id);
        return id;
    }

    const Item &operator[](ItemID id) const noexcept { return _items[static_cast<size_t>(id)]; }

    Item &operator[](ItemID id) noexcept { return _items[static_cast<size_t>(id)]; }

    size_t size() const noexcept { return _items.size(); }
    size_t capacity() const noexcept { return _items.capacity(); }

    void reserve(size_t n) {
        _items.reserve(n);
        _dedup.reserve(n);
    }

    /// Clear for reuse — retains memory capacity.
    void clear() {
        _items.clear();
        _dedup.clear();
    }

    /// Clear and release backing memory to the pool cache.
    /// Cheaper than destruction+reallocation for similar-size runs.
    void release() {
        _items.clear();
        _dedup.clear();
        _pool.release();
    }

  private:
    static size_t _hash_item(const Item &item) noexcept { return std::hash<Item>{}(item); }

    MemoryPool _pool;
    std::pmr::vector<Item> _items{&_pool};
    std::pmr::unordered_map<size_t, ItemID> _dedup{&_pool};

    size_t _max_items{10'000'000};
};
} // namespace algorithm
