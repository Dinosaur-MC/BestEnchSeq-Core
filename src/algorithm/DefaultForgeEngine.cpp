#include "DefaultForgeEngine.h"
#include <stdexcept>

DefaultForgeEngine::DefaultForgeEngine(ForgeConfig config) : _config(config) {}

const ForgeConfig& DefaultForgeEngine::get_config() const noexcept { return _config; }

bool DefaultForgeEngine::is_forgeable(const ItemStack& a, const ItemStack& b) const noexcept {
    return a.is_equipment() || (a.is_book() && b.is_book());
}

std::pair<EnchSet, int32_t> DefaultForgeEngine::combine_enchantments(
    const EnchSet& base, const EnchSet& addition, bool is_book, bool updated) const
{
    // Const version: returns combined set without mutating base
    if (updated)
        return base.combine(addition, is_book);
    else
        return base.combine_s(addition, is_book);
}

int32_t DefaultForgeEngine::calc_penalty_cost(int32_t penalty_a, int32_t penalty_b) const noexcept {
    return ItemStack::get_penalty_cost(penalty_a) + ItemStack::get_penalty_cost(penalty_b);
}

int32_t DefaultForgeEngine::calc_durability(
    const EquipmentType* equipment, int32_t durability_a,
    int32_t durability_b, bool is_equip_b) const noexcept
{
    if (!equipment) return 0;
    if (is_equip_b)
        return equipment->calc_merge_durability(durability_a, durability_b);
    return equipment->calc_repair_durability(durability_a, durability_b);
}

int32_t DefaultForgeEngine::_apply_forge_cost_cap(int32_t raw_cost) const noexcept {
    if (_config.ignore_cost_cap) return raw_cost;
    return raw_cost > 39 ? 39 : raw_cost;
}

std::pair<ItemStack, int32_t> DefaultForgeEngine::forge(
    const ItemStack& item_a, const ItemStack& item_b, bool updated) const
{
    if (!is_forgeable(item_a, item_b))
        throw std::invalid_argument("Invalid item combination: items cannot be forged");

    bool is_book = item_b.is_book();
    auto [combined_ench, ench_cost] = combine_enchantments(
        item_a.enchantments, item_b.enchantments, is_book, updated);

    int32_t cost = ench_cost;

    if (!_config.ignore_penalty_cost)
        cost += calc_penalty_cost(item_a.prior_penalty, item_b.prior_penalty);

    int32_t prior_penalty = 1 + (item_a.prior_penalty >= item_b.prior_penalty
                                  ? item_a.prior_penalty : item_b.prior_penalty);

    bool has_durability = false;
    int32_t durability = 0;
    if (item_a.is_equipment()) {
        if (item_b.is_equipment()) {
            durability = calc_durability(item_a.equipment, item_a.durability, item_b.durability, true);
            has_durability = true;
            if (!_config.ignore_repair_cost)
                cost += 2;
        } else if (!is_book) {
            durability = calc_durability(item_a.equipment, item_a.durability, item_b.durability, false);
            has_durability = true;
            if (!_config.ignore_repair_cost)
                cost += 1;
        }
    }

    // Books or items without durability operation keep original durability
    if (!has_durability && item_a.is_equipment())
        durability = item_a.durability;

    return {
        ItemStack(item_a.equipment, combined_ench, prior_penalty, durability),
        _apply_forge_cost_cap(cost)
    };
}

int32_t DefaultForgeEngine::forge_into(ItemStack& item_a, const ItemStack& item_b, bool updated) const {
    if (!is_forgeable(item_a, item_b))
        throw std::invalid_argument("Invalid item combination: items cannot be forged");

    bool is_book = item_b.is_book();
    auto [combined_ench, ench_cost] = combine_enchantments(
        item_a.enchantments, item_b.enchantments, is_book, updated);

    int32_t cost = ench_cost;

    if (!_config.ignore_penalty_cost)
        cost += calc_penalty_cost(item_a.prior_penalty, item_b.prior_penalty);

    item_a.prior_penalty = 1 + (item_a.prior_penalty >= item_b.prior_penalty
                                  ? item_a.prior_penalty : item_b.prior_penalty);

    // Move-assign combined enchantments — avoids the copy that
    // constructing a new ItemStack would incur.
    item_a.enchantments = std::move(combined_ench);

    if (item_a.is_equipment()) {
        if (item_b.is_equipment()) {
            item_a.durability = calc_durability(item_a.equipment, item_a.durability, item_b.durability, true);
            if (!_config.ignore_repair_cost)
                cost += 2;
        } else if (!is_book) {
            item_a.durability = calc_durability(item_a.equipment, item_a.durability, item_b.durability, false);
            if (!_config.ignore_repair_cost)
                cost += 1;
        }
    }
    // Books and items without durability keep the original durability.

    return _apply_forge_cost_cap(cost);
}
