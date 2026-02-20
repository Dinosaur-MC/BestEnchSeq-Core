#include "ForgeOrder.h"

#include "../algorithm/BaseAlgorithm.h"

ForgeOrder ForgeOrder::make(
    const EnchSet &original_ench, const ItemStack &target_item, const ItemCollection &available_items,
    const std::vector<Step> &steps
) {
    int32_t total_exl_cost     = 0;
    int32_t total_exp_cost     = 0;
    size_t max_cost_step_index = 0;
    for (size_t i = 0; i < steps.size(); i++) {
        total_exl_cost += steps[i].exl_cost;
        total_exp_cost += calc_exp(steps[i].exl_cost);
        if (steps[i].exl_cost > steps[max_cost_step_index].exl_cost)
            max_cost_step_index = i;
    }
    return ForgeOrder({
        original_ench,
        target_item,
        available_items,
        total_exl_cost,
        total_exp_cost,
        steps,
        max_cost_step_index,
        !steps.empty(),
    });
}
