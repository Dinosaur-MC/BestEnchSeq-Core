#include "registries/PlatformConfig.h"
#include "ForgeEngine.h"
#include <algorithm>

// ─── Static helpers ─────────────────────────────────────────────────────────

int32_t ForgeEngine::_penalty_cost(int8_t ppn) noexcept {
    return (1 << ppn) - 1;
}

int32_t ForgeEngine::_apply_cap(int32_t raw) const noexcept {
    if (_ignore_cap) return raw;
    return raw > 39 ? 39 : raw;
}

// ─── Forgeability check ─────────────────────────────────────────────────────

bool ForgeEngine::is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept {
    return a.type == compact::ItemType::Equip
        || (a.type == compact::ItemType::Book && b.type == compact::ItemType::Book);
}

// ─── Forge (mutating) ───────────────────────────────────────────────────────

int32_t ForgeEngine::forge_into(compact::Item& target, const compact::Item& sacrifice,
                                const compact::EnchReg& reg) const
{
    int32_t cost = 0;

    if (!_ignore_penalty)
        cost += _penalty_cost(target.ppn) + _penalty_cost(sacrifice.ppn);

    platform::MCE plat = platform::get_active_platform();
    bool sac_is_book = (sacrifice.type == compact::ItemType::Book);

    for (const auto& se : sacrifice.enchs) {
        bool conflict = false;
        for (const auto& te : target.enchs) {
            if (reg.is_conflict(te.id, se.id)) {
                conflict = true;
                break;
            }
        }

        if (conflict) {
            if (plat == platform::MCE::Java)
                cost += 1;
            continue;
        }

        int32_t mult = sac_is_book
            ? std::max(1, reg.get_multiplier(se.id) >> 1)
            : reg.get_multiplier(se.id);

        auto it = target.enchs.find(se.id);
        if (it != target.enchs.end()) {
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
            target.enchs.insert(se);

            if (mult > 0)
                cost += mult * se.level;
        }
    }

    target.ppn = static_cast<int8_t>(
        1 + (target.ppn >= sacrifice.ppn ? target.ppn : sacrifice.ppn));

    return _apply_cap(cost);
}

// ─── Forge (non-mutating) ───────────────────────────────────────────────────

std::pair<compact::Item, int32_t> ForgeEngine::forge(
    const compact::Item& target, const compact::Item& sacrifice,
    const compact::EnchReg& reg) const
{
    compact::Item result = target;
    int32_t cost = forge_into(result, sacrifice, reg);
    return {std::move(result), cost};
}
