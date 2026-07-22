#pragma once
#include "Item.h"
#include "common/CommonTypes.h"

struct Solution {
    struct EnchStep {
        Item item_a;
        Item item_b;
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
    Item target_item;
    ItemCollection available_items;
    int32_t total_exp_level_cost;
    int32_t total_exp_cost;
    std::vector<EnchStep> steps;
    size_t max_cost_step_index;
    bool is_success;

    bool is_feasible() const;
    int32_t get_peak_level_cost() const;
    int32_t get_peak_exp_cost() const;

    static Solution make(
        MCE platform, const EnchSet &original_ench, const Item &target_item,
        const ItemCollection &available_items, const std::vector<EnchStep> &steps, bool is_valid = true,
        MetaData meta_data = MetaData()
    );
};

using EnchStepList = std::vector<Solution::EnchStep>;
