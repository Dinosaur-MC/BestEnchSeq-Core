#include "ForgeEngine.h"
#include <algorithm>
namespace algorithm {

// ─── IForgeEngine sub-operations ──────────────────────────────────────────────

int32_t ForgeEngine::penalty_cost(int8_t ppn) const noexcept {
    if (_config.ignore_penalty_cost)
        return 0;
    if (ppn < 0 || ppn > 30)
        return INT32_MAX;
    return (1 << ppn) - 1;
}

int32_t ForgeEngine::estimate_forge_cost(
    const Item &target, const Item &sacrifice, const EnchReg &reg
) const noexcept {
    int32_t cost     = penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);
    bool sac_is_book = (sacrifice.type == ItemType::Book);
    for (const auto &e : sacrifice.enchs) {
        int32_t mult = sac_is_book ? reg[e.id].mul_b : reg[e.id].mul;
        cost += e.level * mult;
    }
    return cost;
}

// ─── Forgeability check ─────────────────────────────────────────────────────

bool ForgeEngine::is_forgeable(const Item &a, const Item &b) const noexcept {
    return a.type == ItemType::Equip || (a.type == ItemType::Book && b.type == ItemType::Book);
}

// ─── Forge (mutating) ───────────────────────────────────────────────────────

int32_t ForgeEngine::forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const {
    int32_t cost = 0;

    if (!_config.ignore_penalty_cost)
        cost += penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);

    // Repair cost: equip + equip → +2 if target not at full durability.
    if (target.type == ItemType::Equip && sacrifice.type == ItemType::Equip && !_config.ignore_repair_cost) {
        auto max_dur = reg.get_target_equip().max_durability;
        if (target.dur < max_dur) {
            target.dur = std::min(target.dur + sacrifice.dur + max_dur * 12 / 100, max_dur);
            cost += 2;
        }
    }

    auto plat        = _config.platform;
    bool sac_is_book = (sacrifice.type == ItemType::Book);

    for (const auto &se : sacrifice.enchs) {
        if (target.type == ItemType::Equip && !reg.is_applicable(se.id))
            continue;

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
                new_level = std::min<int16_t>(old_level + 1, reg[se.id].max_lvl);
            else
                new_level = std::max<int16_t>(old_level, se.level);

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

    return cost;
}

// ─── Pure forge (cost-free, for simulate()) ──────────────────────────────────

void ForgeEngine::pure_forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const noexcept {
    // Repair: equip + equip
    if (target.type == ItemType::Equip && sacrifice.type == ItemType::Equip && !_config.ignore_repair_cost) {
        auto max_dur = reg.get_target_equip().max_durability;
        if (target.dur < max_dur) {
            target.dur = std::min(target.dur + sacrifice.dur + max_dur * 12 / 100, max_dur);
        }
    }

    // Enchantment merging (no cost arithmetic)
    for (const auto &se : sacrifice.enchs) {
        if (target.type == ItemType::Equip && !reg.is_applicable(se.id))
            continue;

        bool conflict = false;
        for (const auto &te : target.enchs) {
            if (reg.is_conflict(te.id, se.id)) {
                conflict = true;
                break;
            }
        }
        if (conflict)
            continue;

        auto it = target.enchs.find(se.id);
        if (it != target.enchs.end()) {
            if (it->level == se.level)
                it->level = std::min<int16_t>(it->level + 1, reg[se.id].max_lvl);
            else
                it->level = std::max<int16_t>(it->level, se.level);
        } else {
            target.enchs.insert(se);
        }
    }

    // PPN update
    target.ppn = static_cast<uint8_t>(1 + (target.ppn >= sacrifice.ppn ? target.ppn : sacrifice.ppn));
}

// ─── Forge (non-mutating) ───────────────────────────────────────────────────

std::pair<Item, int32_t>
ForgeEngine::forge(const Item &target, const Item &sacrifice, const EnchReg &reg) const {
    Item result  = target;
    int32_t cost = forge_into(result, sacrifice, reg);
    return {std::move(result), cost};
}

} // namespace algorithm
