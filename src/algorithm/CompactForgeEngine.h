#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "registries/PlatformConfig.h"
#include <cstdint>
#include <utility>

namespace compact {

/// Forge engine operating on compact::Item.
///
/// Rules matching DefaultForgeEngine (Java Edition primary):
///   - Penalty cost = (1 << ppn_a) + (1 << ppn_b) - 2
///   - Enchantment cost per sacrifice enchantment:
///       * Incompatible → +1 (JE) / +0 (BE)
///       * Existing target ench → combine, cost = mult * combined_level (JE)
///       * New ench → add, cost = mult * level
///   - Multiplier = book_mult (book sacrifice) or equip_mult (equip sacrifice)
///   - Result penalty = 2 * max(ppn_a, ppn_b) + 1
///   - Cost capped at 39 levels (configurable)
///
/// Platform is read from platform::get_active_platform() at forge time.
class CompactForgeEngine {
public:
    explicit CompactForgeEngine(bool ignore_penalty_cost = false,
                                bool ignore_cost_cap = false) noexcept
        : _ignore_penalty(ignore_penalty_cost)
        , _ignore_cap(ignore_cost_cap) {}

    /// Forge @p sacrifice into @p target (modifies @p target in-place).
    /// Returns the forge cost in levels.
    int32_t forge_into(Item& target, const Item& sacrifice, const EnchReg& reg) const;

    /// Non-mutating forge. Returns (result_item, cost).
    std::pair<Item, int32_t> forge(const Item& target, const Item& sacrifice,
                                   const EnchReg& reg) const;

    /// Check whether two items can be forged together.
    static bool is_forgeable(const Item& a, const Item& b) noexcept;

private:
    static int32_t _penalty_cost(int8_t ppn) noexcept;
    int32_t _apply_cap(int32_t raw) const noexcept;

    bool _ignore_penalty;
    bool _ignore_cap;
};

} // namespace compact
