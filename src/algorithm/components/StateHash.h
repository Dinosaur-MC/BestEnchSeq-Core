#pragma once
#include "algorithm/components/ItemPool.h"
#include "utils/HashUtils.hpp"
#include <cstddef>
#include <vector>

/// Hash utilities for algorithm-internal states.
namespace StateHash {

/// Hash a single Item's content (independent of any pool).
inline size_t item(const compact::Item& item) noexcept {
    size_t h = static_cast<size_t>(item.type)
             ^ (static_cast<size_t>(item.ppn) << 8)
             ^ (static_cast<size_t>(item.dur) << 16);
    hash_combine(h, item.enchs.hash());
    return h;
}

/// Hash a vector of ItemIDs (resolves through pool).
inline size_t ids(const std::vector<ItemPool::ItemID>& ids,
                  const ItemPool& pool) noexcept
{
    size_t h = ids.size();
    for (auto id : ids)
        hash_combine(h, item(pool[id]));
    return h;
}

} // namespace StateHash
