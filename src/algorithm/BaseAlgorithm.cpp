#include "BaseAlgorithm.h"

BaseAlgorithm::State BaseAlgorithm::get_state() const { return _state; }
BaseAlgorithm::Output BaseAlgorithm::get_output() const {
    if (_state != State::Finished)
        return {.is_valid = false};
    return _output;
}

std::pair<ItemStack, int32_t>
BaseAlgorithm::forge_item(const ItemStack &item_a, const ItemStack &item_b, bool updated) const {
    auto combination_ret =
        updated ? item_a.enchantments.combine(item_b.enchantments, item_b.get_multiplier_index())
                : item_a.enchantments.combine_s(item_b.enchantments, item_b.get_multiplier_index());

    int32_t cost = combination_ret.second;
    if (!_config.ignore_penalty_cost)
        cost += ItemStack::get_penalty_cost(item_a.prior_penalty) +
                ItemStack::get_penalty_cost(item_b.prior_penalty);

    int32_t prior_penalty =
        1 + (item_a.prior_penalty >= item_b.prior_penalty ? item_a.prior_penalty : item_b.prior_penalty);

    int32_t durability = 0;
    if (item_a.is_equipment()) {
        if (item_b.is_equipment()) {
            durability = item_a.equipment->calc_merge_durability(item_a.durability, item_b.durability);
            if (!_config.ignore_repair_cost)
                cost += 2;
        } else if (!item_b.is_book()) {
            durability = item_a.equipment->calc_repair_durability(item_a.durability, item_b.durability);
            if (!_config.ignore_repair_cost)
                cost += 1;
        }
    }

    return {
        {
            item_a.equipment,
            combination_ret.first,
            prior_penalty,
            durability,
        },
        cost,
    };
}

int32_t calc_exp(int32_t level) {
    if (level <= 16)
        return level * level + 6 * level;
    else if (level <= 31)
        return 2.5 * level * level - 40.5 * level + 360;
    else
        return 4.5 * level * level - 162.5 * level + 2220;
}
