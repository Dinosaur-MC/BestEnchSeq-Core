#include "EnchSolution.h"

#include "../utils/ExpCalculator.hpp"

bool EnchSolution::is_feasible() const { return is_success && steps.size() > 0; }
int32_t EnchSolution::get_peek_level_cost() const {
    if (!is_feasible() || max_cost_step_index >= steps.size() || max_cost_step_index < 0)
        return 0;
    return steps[max_cost_step_index].exp_level_cost;
}
int32_t EnchSolution::get_peek_exp_cost() const {
    if (!is_feasible() || max_cost_step_index >= steps.size() || max_cost_step_index < 0)
        return 0;
    return steps[max_cost_step_index].exp_cost;
}

EnchSolution EnchSolution::make(
    platform::MCE platform, const EnchSet &original_ench, const ItemStack &target_item,
    const ItemCollection &available_items, const EnchStepList &steps, bool is_valid, MetaData meta_data
) {
    int32_t total_exp_level_cost = 0;
    int32_t total_exp_cost       = 0;
    size_t max_cost_step_index   = 0;
    for (size_t i = 0; i < steps.size(); i++) {
        total_exp_level_cost += steps[i].exp_level_cost;
        total_exp_cost += ExpCalculator::level_to_exp(steps[i].exp_level_cost);
        if (steps[i].exp_level_cost > steps[max_cost_step_index].exp_level_cost)
            max_cost_step_index = i;
    }
    return EnchSolution({
        meta_data,
        platform,
        original_ench,
        target_item,
        available_items,
        total_exp_level_cost,
        total_exp_cost,
        steps,
        max_cost_step_index,
        is_valid,
    });
}
