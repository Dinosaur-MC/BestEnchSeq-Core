#include "ForgeEngine.h"
#include <algorithm>

// ─── IForgeEngine sub-operations ──────────────────────────────────────────────

int32_t ForgeEngine::penalty_cost(int8_t ppn) const noexcept {
    if (_config.ignore_penalty_cost)
        return 0;
    if (ppn < 0 || ppn > 30)
        return INT32_MAX;
    return (1 << ppn) - 1;
}

int32_t ForgeEngine::apply_cap(int32_t raw_cost) const noexcept {
    if (_config.ignore_cost_cap)
        return raw_cost;
    return raw_cost > 39 ? 39 : raw_cost;
}

int32_t ForgeEngine::estimate_forge_cost(const compact::Item &target, const compact::Item &sacrifice,
                                         const compact::EnchReg &reg) const noexcept {
    int32_t cost = penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);
    bool sac_is_book = (sacrifice.type == compact::ItemType::Book);
    for (const auto &e : sacrifice.enchs) {
        int32_t mult = sac_is_book ? reg[e.id].mul_b : reg[e.id].mul;
        cost += e.level * mult;
    }
    return cost;
}

// ─── Forgeability check ─────────────────────────────────────────────────────

bool ForgeEngine::is_forgeable(const compact::Item &a, const compact::Item &b) const noexcept {
    return a.type == compact::ItemType::Equip || (a.type == compact::ItemType::Book && b.type == compact::ItemType::Book);
}

// ─── Forge (mutating) ───────────────────────────────────────────────────────

int32_t ForgeEngine::forge_into(compact::Item &target, const compact::Item &sacrifice, const compact::EnchReg &reg) const {
    int32_t cost = 0;

    if (!_config.ignore_penalty_cost)
        cost += penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);

    // Repair cost: equip + equip → +2 if target not at full durability.
    if (target.type == compact::ItemType::Equip && sacrifice.type == compact::ItemType::Equip && !_config.ignore_repair_cost) {
        auto max_dur = reg.get_target_equip().max_durability;
        if (target.dur < max_dur) {
            target.dur = std::min(target.dur + sacrifice.dur + max_dur * 12 / 100, max_dur);
            cost += 2;
        }
    }

    auto plat = _config.platform;
    bool sac_is_book = (sacrifice.type == compact::ItemType::Book);

    for (const auto &se : sacrifice.enchs) {
        bool conflict = false;
        for (const auto &te : target.enchs) {
            if (reg.is_conflict(te.id, se.id)) {
                conflict = true;
                break;
            }
        }

        if (conflict) {
            if (plat == MCE::Java)
                cost += 1;
            continue;
        }

        int32_t mult = sac_is_book ? reg[se.id].mul_b : reg[se.id].mul;

        auto it = target.enchs.find(se.id);
        if (it != target.enchs.end()) {
            int16_t old_level = it->level;
            int16_t new_level;
            if (old_level == se.level)
                new_level = static_cast<int16_t>(std::min<int32_t>(old_level + 1, reg[se.id].max_lvl));
            else
                new_level = static_cast<int16_t>(std::max<int32_t>(old_level, se.level));

            it->level = new_level;

            if (mult > 0) {
                if (plat == MCE::Java)
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

    target.ppn = static_cast<uint8_t>(1 + (target.ppn >= sacrifice.ppn ? target.ppn : sacrifice.ppn));

    return apply_cap(cost);
}

// ─── Forge (non-mutating) ───────────────────────────────────────────────────

std::pair<compact::Item, int32_t> ForgeEngine::forge(const compact::Item &target, const compact::Item &sacrifice,
                                                     const compact::EnchReg &reg) const {
    compact::Item result = target;
    int32_t cost = forge_into(result, sacrifice, reg);
    return {std::move(result), cost};
}
