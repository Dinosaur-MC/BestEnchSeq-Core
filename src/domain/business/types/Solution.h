#pragma once
#include "Item.h"
#include "common/CommonTypes.h"
#include <chrono>
#include <vector>

struct Solution;

// Standalone to avoid Clang restriction: default member initializers in nested
// classes trigger an error when used as a default argument in the enclosing
// class.  Qualified as Solution::MetaData via `using` inside Solution.
struct SolutionMetaData {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time;
    AlgorithmMode mode = AlgorithmMode::direct;
    size_t task_id     = 0;
};

struct Solution {
    using MetaData = SolutionMetaData;

    struct EnchStep {
        Item item_a;
        Item item_b;
        int32_t exp_level_cost;
        int32_t exp_cost;
    };

    MetaData metadata;

    MCE platform;
    EnchSet original_ench;
    Item target_item;
    std::vector<Item> available_items;
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
        const std::vector<Item> &available_items, const std::vector<EnchStep> &steps, bool is_valid = true,
        MetaData meta_data = MetaData{}
    );

    bool operator<(const Solution &o) const {
        return total_exp_cost == o.total_exp_cost ? get_peak_level_cost() < o.get_peak_level_cost()
                                                  : total_exp_cost < o.total_exp_cost;
    }
};

using EnchStepList = std::vector<Solution::EnchStep>;
