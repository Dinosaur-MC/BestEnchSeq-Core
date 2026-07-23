#pragma once
#include "domain/algorithm/registries/EnchReg.h"
#include "ItemPool.h"
#include "SearchUtils.h"
#include <cstdint>
#include <vector>

namespace algorithm {

/// Admissible heuristic for direct `vector<Item>` states (no ItemPool).
///
/// Computes a lower bound on remaining cost: for each missing target
/// enchantment level, adds (missing_level * book_multiplier). Ignores
/// penalties, conflicts, and cost caps — always ≤ real cost.
///
/// Caller provides scratch buffers to avoid per-call heap allocation.
namespace HeuristicBasic {

inline int32_t compute(
    const std::vector<Item> &items, const EnchReg &reg, const std::vector<Ench> &target,
    std::vector<int16_t> &buf, std::vector<int16_t> &dirty
) {
    if (items.empty())
        return 0;

    search_utils::fill_max_levels(
        [&](auto &&yield) {
            for (const auto &item : items)
                for (const auto &e : item.enchs)
                    if (e.id >= 0)
                        yield(e.id, e.level);
        },
        reg, buf, dirty
    );

    int32_t h = search_utils::compute_h(target, reg, buf);

    for (auto id : dirty) {
        buf[id] = 0;
    }

    return h;
}

} // namespace HeuristicBasic

/// Admissible heuristic for enchanting path search.
///
/// Computes a lower bound on remaining cost: for each missing target
/// enchantment level, adds (missing_level * book_multiplier). Ignores
/// penalties, conflicts, and cost caps — always ≤ real cost.
///
/// Caller provides scratch buffers to avoid per-call heap allocation.
namespace Heuristic {

inline int32_t compute(
    const std::vector<ItemPool::ItemID> &ids, const ItemPool &pool, const EnchReg &reg,
    const std::vector<Ench> &target,
    std::vector<int16_t> &buf, // reusable scratch; resized if needed
    std::vector<int16_t> &dirty
) // reusable dirty-id tracker
{
    if (ids.empty())
        return 0;

    search_utils::fill_max_levels(
        [&](auto &&yield) {
            for (auto id : ids)
                for (const auto &e : pool[id].enchs)
                    if (e.id >= 0)
                        yield(e.id, e.level);
        },
        reg, buf, dirty
    );

    int32_t h = search_utils::compute_h(target, reg, buf);

    for (auto id : dirty) {
        buf[id] = 0;
    }

    return h;
}

} // namespace Heuristic
} // namespace algorithm