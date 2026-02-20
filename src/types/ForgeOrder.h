#pragma once
#include "ItemStack.h"

/*
 * 锻造顺序存储容器
 */
struct ForgeOrder {
    struct Step {
        std::string preparation;
        ItemStack item_a;
        ItemStack item_b;
        int32_t exl_cost;
        int32_t exp_cost;
    };

    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
    int32_t total_exl_cost;
    int32_t total_exp_cost;
    std::vector<Step> steps;
    size_t max_cost_step_index;
    size_t calculation_time;
};
