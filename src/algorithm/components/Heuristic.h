#pragma once
#include "algorithm/components/ItemPool.h"
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
    int32_t (*book_multiplier_fn)(int32_t),
    std::vector<int16_t>& buf,     // reusable scratch; resized if needed
    std::vector<int16_t>& dirty)   // reusable dirty-id tracker
{
    int32_t h = 0;
    if (ids.empty()) return h;

    if (buf.size() < reg.size())
        buf.assign(reg.size(), 0);
    dirty.clear();

    for (auto id : ids) {
        for (const auto& e : pool[id].enchs) {
            if (e.level > buf[e.id]) {
                if (buf[e.id] == 0)
                    dirty.push_back(e.id);
                buf[e.id] = e.level;
            }
        }
    }

    for (const auto& t : target) {
        int16_t have = buf[t.id];
        if (have < t.level)
            h += (t.level - have) * book_multiplier_fn(reg.get_multiplier(t.id));
    }

    for (auto id : dirty)
        buf[id] = 0;

    return h;
}

} // namespace Heuristic
