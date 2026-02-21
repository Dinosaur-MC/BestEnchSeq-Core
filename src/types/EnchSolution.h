#pragma once
#include "ItemStack.h"

struct EnchSolution {
    struct Step {
        ItemStack item_a;
        ItemStack item_b;
        int32_t exp_level_cost;
        int32_t exp_cost;
        std::string pre_operator;
        std::string post_operator;
    };

    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
    int32_t total_exp_level_cost;
    int32_t total_exp_cost;
    std::vector<Step> steps;
    size_t max_cost_step_index;
    bool is_success;

    bool is_feasible() const;
    int32_t get_peek_level_cost() const;
    int32_t get_peek_exp_cost() const;

    static EnchSolution make(
        const EnchSet &original_ench, const ItemStack &target_item, const ItemCollection &available_items,
        const std::vector<Step> &steps
    );
};

using EnchStepList = std::vector<EnchSolution::Step>;
