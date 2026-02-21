#include "BaseAlgorithm.h"

#include <stdexcept>

void BaseAlgorithm::init(const Config &config) {
    if (_state == Running)
        return;
    _state  = None;
    _config = config;
    _init(config);
    _state = Ready;
}
void BaseAlgorithm::run(const Input &input) {
    if (_state != Ready)
        return;
    _state = Running;
    _input = input;
    _run(input);
}
void BaseAlgorithm::stop() {
    if (_state != Running)
        return;
    _state = _stop() ? Finished : Ready;
}

BaseAlgorithm::State BaseAlgorithm::get_state() const noexcept { return _state; }
BaseAlgorithm::Output BaseAlgorithm::get_output() const {
    if (_state != State::Finished)
        return {.is_valid = false};
    return _output;
}

std::pair<ItemStack, int32_t>
BaseAlgorithm::forge_item(const ItemStack &item_a, const ItemStack &item_b, bool updated) const {
    if (!Utils::is_forgeable(item_a, item_b))
        throw std::invalid_argument("Invalid item combination");

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

namespace Utils {

int32_t calc_exp(int32_t level) {
    if (level <= 16)
        return level * level + 6 * level;
    else if (level <= 31)
        return 2.5 * level * level - 40.5 * level + 360;
    else
        return 4.5 * level * level - 162.5 * level + 2220;
}

bool is_forgeable(const ItemStack &item_a, const ItemStack &item_b) {
    return item_a.is_equipment() || item_a.is_book() && item_b.is_book();
}

std::pair<ItemStack, int32_t> forge_item(const ItemStack &item_a, const ItemStack &item_b, bool updated) {
    if (!is_forgeable(item_a, item_b))
        throw std::invalid_argument("Invalid item combination");

    auto combination_ret =
        updated ? item_a.enchantments.combine(item_b.enchantments, item_b.get_multiplier_index())
                : item_a.enchantments.combine_s(item_b.enchantments, item_b.get_multiplier_index());

    int32_t cost = combination_ret.second + ItemStack::get_penalty_cost(item_a.prior_penalty) +
                   ItemStack::get_penalty_cost(item_b.prior_penalty);

    int32_t prior_penalty =
        1 + (item_a.prior_penalty >= item_b.prior_penalty ? item_a.prior_penalty : item_b.prior_penalty);

    int32_t durability = 0;
    if (item_a.is_equipment()) {
        if (item_b.is_equipment()) {
            durability = item_a.equipment->calc_merge_durability(item_a.durability, item_b.durability);
            cost += 2;
        } else if (!item_b.is_book()) {
            durability = item_a.equipment->calc_repair_durability(item_a.durability, item_b.durability);
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

EnchSolution make_solution(const BaseAlgorithm::Input &input, const BaseAlgorithm::Output &output) {
    return EnchSolution::make(
        input.platform, input.original_ench, input.target_item, input.available_items, output.steps,
        output.is_valid,
        {
            output.algorithm_name,
            output.algorithm_version,
            output.created_at,
            output.computation_time,
        }
    );
}

}; // namespace Utils
