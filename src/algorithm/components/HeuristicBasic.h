#pragma once
#include "algorithm/components/SearchUtils.h"
#include "registries/CompactedRegistries.h"
#include "types/CompactedTypes.h"
#include <cstdint>
#include <vector>

/// Admissible heuristic for direct `vector<Item>` states (no ItemPool).
///
/// Computes a lower bound on remaining cost: for each missing target
/// enchantment level, adds (missing_level * book_multiplier). Ignores
/// penalties, conflicts, and cost caps — always ≤ real cost.
///
/// Caller provides scratch buffers to avoid per-call heap allocation.
namespace HeuristicBasic {

inline int32_t compute(
    const std::vector<compact::Item>& items,
    const compact::EnchReg& reg,
    const std::vector<compact::Ench>& target,
    std::vector<int16_t>& buf,
    std::vector<int16_t>& dirty)
{
    if (items.empty()) return 0;

    search_utils::fill_max_levels(
        [&](auto&& yield) {
            for (const auto& item : items)
                for (const auto& e : item.enchs)
                    if (e.id >= 0) yield(e.id, e.level);
        },
        reg, buf, dirty);

    int32_t h = search_utils::compute_h(target, reg, buf);

    for (auto id : dirty) {
        buf[id] = 0;
    }

    return h;
}

} // namespace HeuristicBasic
