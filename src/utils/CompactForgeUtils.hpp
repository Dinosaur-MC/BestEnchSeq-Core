#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include <algorithm>
#include <cstdint>

namespace compact {

/// Book multiplier for cost estimation (JE rule: max(1, equip_mult >> 1)).
inline int32_t book_multiplier(int32_t equip_mult) noexcept {
    return std::max(1, equip_mult >> 1);
}

/// Estimate forge cost: penalty cost + sum of sacrifice enchantment costs.
/// Used for pair sorting / heuristic (not actual forge cost).
inline int32_t estimate_forge_cost(const Item& target, const Item& sacrifice,
                                   const EnchReg& reg) noexcept {
    int32_t cost = ((1 << target.ppn) - 1) + ((1 << sacrifice.ppn) - 1);
    bool sac_is_book = (sacrifice.type == ItemType::Book);
    for (const auto& e : sacrifice.enchs) {
        int32_t mult = sac_is_book
            ? book_multiplier(reg.get_multiplier(e.id))
            : reg.get_multiplier(e.id);
        cost += e.level * mult;
    }
    return cost;
}

} // namespace compact
