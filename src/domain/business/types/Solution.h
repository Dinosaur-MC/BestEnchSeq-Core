#pragma once
#include "Item.h"
#include "common/CommonTypes.h"
#include "common/serialization/IJsonSerializable.h"
#include <chrono>
#include <vector>

struct Solution;

// Standalone to avoid Clang restriction: default member initializers in nested
// classes trigger an error when used as a default argument in the enclosing
// class.  Qualified as Solution::MetaData via `using` inside Solution.
struct SolutionMetaData : IJsonSerializable {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time;
    AlgorithmMode mode = AlgorithmMode::direct;
    size_t task_id     = 0;

    // Default + aggregate-emulating constructors
    SolutionMetaData() = default;
    SolutionMetaData(std::string name, std::string version,
                     std::chrono::system_clock::time_point at,
                     std::chrono::milliseconds ct)
        : algorithm_name(std::move(name)), algorithm_version(std::move(version)),
          created_at(at), computation_time(ct) {}

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

struct Solution : IJsonSerializable {
    using MetaData = SolutionMetaData;

    struct EnchStep : IJsonSerializable {
        Item item_a;
        Item item_b;
        int32_t exp_level_cost;
        int32_t exp_cost;

        // Default + aggregate-emulating constructors
        EnchStep() = default;
        EnchStep(Item a, Item b, int32_t level_cost, int32_t cost)
            : item_a(std::move(a)), item_b(std::move(b)),
              exp_level_cost(level_cost), exp_cost(cost) {}

        // -- ISerializable --
        Json to_json() const override;
        void from_json(const Json& json) override;
    };

    // Default + aggregate-emulating constructor
    Solution() = default;
    Solution(MetaData md, MCE plat, EnchSet oe, Item ti,
             std::vector<Item> ai, int32_t telc, int32_t tec,
             std::vector<EnchStep> s, size_t mcsi, bool iss)
        : metadata(std::move(md)), platform(plat),
          original_ench(std::move(oe)), target_item(std::move(ti)),
          available_items(std::move(ai)),
          total_exp_level_cost(telc), total_exp_cost(tec),
          steps(std::move(s)), max_cost_step_index(mcsi), is_success(iss) {}

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

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;

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
