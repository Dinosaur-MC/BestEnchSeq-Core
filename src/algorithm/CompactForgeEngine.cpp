#include "registries/PlatformConfig.h"
#include "CompactForgeEngine.h"
#include <algorithm>

namespace compact {

// ─── Static helpers ─────────────────────────────────────────────────────────

int32_t CompactForgeEngine::_penalty_cost(int8_t ppn) noexcept {
    return (1 << ppn) - 1;
}

int32_t CompactForgeEngine::_apply_cap(int32_t raw) const noexcept {
    if (_ignore_cap) return raw;
    return raw > 39 ? 39 : raw;
}

// ─── Forgeability check ─────────────────────────────────────────────────────

bool CompactForgeEngine::is_forgeable(const Item& a, const Item& b) noexcept {
    // Target must be equipment, or both must be books
    return a.type == ItemType::Equip || (a.type == ItemType::Book && b.type == ItemType::Book);
}

// ─── Forge (mutating) ───────────────────────────────────────────────────────

int32_t CompactForgeEngine::forge_into(Item& target, const Item& sacrifice,
                                       const EnchReg& reg) const
{
    int32_t cost = 0;

    // 1. Penalty cost
    if (!_ignore_penalty)
        cost += _penalty_cost(target.ppn) + _penalty_cost(sacrifice.ppn);

    platform::MCE plat = platform::get_active_platform();
    bool sac_is_book = (sacrifice.type == ItemType::Book);

    // 2. Process each sacrifice enchantment
    for (const auto& se : sacrifice.enchs) {
        // 2a. Check incompatibility with existing target enchantments
        bool conflict = false;
        for (const auto& te : target.enchs) {
            if (reg.is_conflict(te.id, se.id)) {
                conflict = true;
                break;
            }
        }

        if (conflict) {
            // Incompatible — skip the enchantment
            if (plat == platform::MCE::Java)
                cost += 1;
            continue;
        }

        // 2b. Determine multiplier based on sacrifice type
        int32_t mult = sac_is_book
            ? std::max(1, reg.get_multiplier(se.id) >> 1)
            : reg.get_multiplier(se.id);

        // 2c. Binary-search for this enchantment on target (enchs is sorted by id)
        auto it = std::lower_bound(
            target.enchs.begin(), target.enchs.end(), se.id,
            [](const Ench& e, int16_t id) { return e.id < id; });

        if (it != target.enchs.end() && it->id == se.id) {
            // Existing enchantment — combine levels
            int16_t old_level = it->level;
            int16_t new_level;
            if (old_level == se.level)
                new_level = static_cast<int16_t>(
                    std::min<int32_t>(old_level + 1, reg.get_max_level(se.id)));
            else
                new_level = static_cast<int16_t>(
                    std::max<int32_t>(old_level, se.level));

            it->level = new_level;

            if (mult > 0) {
                if (plat == platform::MCE::Java)
                    cost += mult * new_level;
                else
                    cost += mult * (new_level - old_level);
            }
        } else {
            // New enchantment — insert at sorted position by id.
            // This maintains canonical ordering so two items with the same
            // enchantments compare equal regardless of forge sequence.
            auto pos = std::lower_bound(
                target.enchs.begin(), target.enchs.end(), se.id,
                [](const Ench& e, int16_t id) { return e.id < id; });
            target.enchs.insert(pos, se);

            if (mult > 0)
                cost += mult * se.level;
        }
    }

    // 3. Update penalty count: result = 1 + max(ppn_a, ppn_b)
    target.ppn = static_cast<int8_t>(
        1 + (target.ppn >= sacrifice.ppn ? target.ppn : sacrifice.ppn));

    return _apply_cap(cost);
}

// ─── Forge (non-mutating) ───────────────────────────────────────────────────

std::pair<Item, int32_t> CompactForgeEngine::forge(
    const Item& target, const Item& sacrifice, const EnchReg& reg) const
{
    Item result = target;
    int32_t cost = forge_into(result, sacrifice, reg);
    return {std::move(result), cost};
}

} // namespace compact
