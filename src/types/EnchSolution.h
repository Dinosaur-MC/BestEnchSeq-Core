#pragma once
#include "ForgeConfig.h"
#include "ItemStack.h"

struct EnchSolution {
    struct EnchStep {
        ItemStack item_a;
        ItemStack item_b;
        int32_t exp_level_cost;
        int32_t exp_cost;
    };

    struct MetaData {
        std::string algorithm_name;
        std::string version;
        size_t created_at;
        size_t computation_time;
    } metadata;

    MCE platform;
    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
    int32_t total_exp_level_cost;
    int32_t total_exp_cost;
    std::vector<EnchStep> steps;
    size_t max_cost_step_index;
    bool is_success;

    bool is_feasible() const;
    int32_t get_peek_level_cost() const;
    int32_t get_peek_exp_cost() const;

    static EnchSolution make(
        MCE platform, const EnchSet &original_ench, const ItemStack &target_item,
        const ItemCollection &available_items, const std::vector<EnchStep> &steps, bool is_valid = true,
        MetaData meta_data = MetaData()
    );
};

using EnchStepList = std::vector<EnchSolution::EnchStep>;

struct EnhancedEnchStep : public EnchSolution::EnchStep {
    std::vector<std::string> pre_operator;
    std::vector<std::string> post_operator;
};
