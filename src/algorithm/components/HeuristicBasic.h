#pragma once
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
    int32_t h = 0;
    if (items.empty()) return h;

    if (buf.size() < reg.size())
        buf.assign(reg.size(), 0);
    dirty.clear();

    for (const auto& item : items) {
        for (const auto& e : item.enchs) {
            if (e.id < 0) continue;
            if (e.level > buf[e.id]) {
                if (buf[e.id] == 0)
                    dirty.push_back(e.id);
                buf[e.id] = e.level;
            }
        }
    }

    for (const auto& t : target) {
        if (t.id < 0) continue;
        int16_t have = buf[t.id];
        if (have < t.level)
            h += (t.level - have) * reg[t.id].mul_b;
    }

    for (auto id : dirty) {
        if (id < 0) continue;
        buf[id] = 0;
    }

    return h;
}

} // namespace HeuristicBasic
