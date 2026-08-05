#include "ForgeEngine.h"
#include <algorithm>

namespace algorithm {

// ─── IForgeEngine sub-operations ──────────────────────────────────────────────

int32_t ForgeEngine::penalty_cost(int8_t ppn) const noexcept {
    if (ppn < 0 || ppn > 30)
        return INT32_MAX;
    return (1 << ppn) - 1;
}

int32_t ForgeEngine::estimate_forge_cost(
    const Item &target, const Item &sacrifice, const EnchReg &reg
) const noexcept {
    int32_t cost = _config.ignore_penalty_cost ? 0 : penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);
    bool sac_is_book = (sacrifice.type == ItemType::Book);
    for (sbit_iterator<EnchSet::mask_type, uint8_t> it(sacrifice.enchs.get_mask()); it; ++it) {
        int32_t mult = sac_is_book ? reg[*it].mul_b : reg[*it].mul;
        cost += sacrifice.enchs[*it] * mult;
    }
    return cost;
}

// ─── Forgeability check ─────────────────────────────────────────────────────

bool ForgeEngine::is_forgeable(const Item &a, const Item &b) const noexcept {
    return a.type == ItemType::Equip || (a.type == ItemType::Book && b.type == ItemType::Book);
}

// ─── Forge (mutating) ───────────────────────────────────────────────────────

int32_t ForgeEngine::forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const {
    // Reject invalid pairs — defensive guard; callers should check is_forgeable() first.
    if (!is_forgeable(target, sacrifice))
        return INT32_MAX;

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
    auto same        = target.enchs & sacrifice.enchs;
    auto diff        = sacrifice.enchs - target.enchs;
    bit_iterator<EnchSet::mask_type, uint8_t> it(diff);
    for (auto i = it.next(); i != it.npos; i = it.next()) {
        if (target.type == ItemType::Book || reg.is_applicable(i)) {
            if (!_config.ignore_imcompatible) {
                auto conflict_mask = target.enchs & reg.get_conflict_mask(i);
                if (conflict_mask) {
                    if (plat == MCE::Java)
                        cost += std::popcount(conflict_mask);
                    continue;
                }
            }
            auto lvl = sacrifice.enchs[i];
            cost += lvl * (sac_is_book ? reg[i].mul_b : reg[i].mul);
            target.enchs.insert(i, lvl);
        }
    }
    it.reset(same);
    for (auto i = it.next(); i != it.npos; i = it.next()) {
        auto lvl1 = target.enchs[i];
        auto lvl2 = sacrifice.enchs[i];
        if (lvl1 == lvl2)
            lvl2 = std::min<uint8_t>(lvl2 + 1, reg[i].max_lvl);
        else
            lvl2 = std::max<uint8_t>(lvl1, lvl2);
        target.enchs.insert(i, lvl2);
        cost += (plat == MCE::Java ? lvl2 : lvl2 - lvl1) * (sac_is_book ? reg[i].mul_b : reg[i].mul);
    }

    target.ppn = static_cast<uint8_t>(1 + std::max(target.ppn, sacrifice.ppn));

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
    auto same = target.enchs & sacrifice.enchs;
    auto diff = sacrifice.enchs - target.enchs;
    bit_iterator<EnchSet::mask_type, uint8_t> it(diff);
    for (auto i = it.next(); i < it.npos; i = it.next()) {
        if (target.type == ItemType::Book || reg.is_applicable(i)) {
            if (!_config.ignore_imcompatible) {
                auto conflict_mask = target.enchs & reg.get_conflict_mask(i);
                if (conflict_mask)
                    continue;
            }
            target.enchs.insert(i, sacrifice.enchs[i]);
        }
    }
    it.reset(same);
    for (auto i = it.next(); i < it.npos; i = it.next()) {
        auto lvl1 = target.enchs[i];
        auto lvl2 = sacrifice.enchs[i];
        if (lvl1 == lvl2)
            lvl2 = std::min<uint8_t>(lvl2 + 1, reg[i].max_lvl);
        else
            lvl2 = std::max<uint8_t>(lvl1, lvl2);
        target.enchs.insert(i, lvl2);
    }

    // PPN update
    target.ppn = static_cast<uint8_t>(1 + std::max(target.ppn, sacrifice.ppn));
}

// ─── Forge (non-mutating) ───────────────────────────────────────────────────

std::pair<Item, int32_t>
ForgeEngine::forge(const Item &target, const Item &sacrifice, const EnchReg &reg) const {
    Item result  = target;
    int32_t cost = forge_into(result, sacrifice, reg);
    return {std::move(result), cost};
}

} // namespace algorithm
