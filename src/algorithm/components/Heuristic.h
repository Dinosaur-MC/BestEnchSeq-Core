#pragma once
#include "algorithm/components/ItemPool.h"
#include "algorithm/components/SearchUtils.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

/// Admissible heuristic for enchanting path search.
///
/// Computes a lower bound on remaining cost: for each missing target
/// enchantment level, adds (missing_level * book_multiplier). Ignores
/// penalties, conflicts, and cost caps — always ≤ real cost.
///
/// Caller provides scratch buffers to avoid per-call heap allocation.
namespace Heuristic {

inline int32_t compute(
    const std::vector<ItemPool::ItemID>& ids,
    const ItemPool& pool,
    const compact::EnchReg& reg,
    const std::vector<compact::Ench>& target,
    std::vector<int16_t>& buf,     // reusable scratch; resized if needed
    std::vector<int16_t>& dirty)   // reusable dirty-id tracker
{
    int32_t h = 0;
    if (ids.empty()) return h;

    search_utils::fill_max_levels(
        [&](auto&& yield) {
            for (auto id : ids)
                for (const auto& e : pool[id].enchs)
                    if (e.id >= 0) yield(e.id, e.level);
        },
        reg, buf, dirty);

    h = search_utils::compute_h(target, reg, buf);

    for (auto id : dirty) {
        buf[id] = 0;
    }

    return h;
}

} // namespace Heuristic
