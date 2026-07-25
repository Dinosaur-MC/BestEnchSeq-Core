#include "Solution.h"
#include "domain/business/components/Serializer.h"
#include "common/utils/ExpCalculator.hpp"

#include <chrono>

// ============================================================================
// Solution::EnchStep
// ============================================================================

Json Solution::EnchStep::to_json() const {
    return Json::object()
        .set("item_a", item_a.to_json())
        .set("item_b", item_b.to_json())
        .set("exp_level_cost", exp_level_cost)
        .set("exp_cost", exp_cost);
}

void Solution::EnchStep::from_json(const Json& json) {
    if (json.has("item_a"))
        item_a.from_json(json["item_a"]);
    if (json.has("item_b"))
        item_b.from_json(json["item_b"]);
    if (json.has("exp_level_cost"))
        exp_level_cost = static_cast<int32_t>(json["exp_level_cost"].as<int64_t>());
    if (json.has("exp_cost"))
        exp_cost = static_cast<int32_t>(json["exp_cost"].as<int64_t>());
}

// ============================================================================
// SolutionMetaData
// ============================================================================

Json SolutionMetaData::to_json() const {
    return Json::object()
        .set("algorithm_name", algorithm_name)
        .set("algorithm_version", algorithm_version)
        .set("created_at", static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                created_at.time_since_epoch()).count()))
        .set("computation_time", static_cast<int64_t>(computation_time.count()))
        .set("mode", static_cast<int8_t>(mode))
        .set("task_id", static_cast<int64_t>(task_id));
}

void SolutionMetaData::from_json(const Json& json) {
    if (json.has("algorithm_name"))
        algorithm_name = json["algorithm_name"].as<std::string>();
    if (json.has("algorithm_version"))
        algorithm_version = json["algorithm_version"].as<std::string>();
    if (json.has("created_at")) {
        auto ms = json["created_at"].as<int64_t>();
        created_at = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
    }
    if (json.has("computation_time"))
        computation_time = std::chrono::milliseconds(json["computation_time"].as<int64_t>());
    if (json.has("mode"))
        mode = static_cast<AlgorithmMode>(static_cast<int32_t>(json["mode"].as<int64_t>()));
    if (json.has("task_id"))
        task_id = static_cast<size_t>(json["task_id"].as<int64_t>());
}

// ============================================================================
// Solution
// ============================================================================

Json Solution::to_json() const {
    // Build available_items array
    Json items_arr = Json::array();
    for (const auto& item : available_items)
        items_arr.push_back(item.to_json());

    // Build steps array
    Json steps_arr = Json::array();
    for (const auto& step : steps)
        steps_arr.push_back(step.to_json());

    return Json::object()
        .set("metadata", metadata.to_json())
        .set("platform", std::string(Serializer::mce_to_string(platform)))
        .set("original_ench", original_ench.to_json())
        .set("target_item", target_item.to_json())
        .set("available_items", items_arr)
        .set("total_exp_level_cost", total_exp_level_cost)
        .set("total_exp_cost", total_exp_cost)
        .set("steps", steps_arr)
        .set("max_cost_step_index", static_cast<int64_t>(max_cost_step_index))
        .set("is_success", is_success);
}

void Solution::from_json(const Json& json) {
    if (json.has("metadata"))
        metadata.from_json(json["metadata"]);
    if (json.has("platform"))
        platform = Serializer::string_to_mce(json["platform"].as<std::string>());
    if (json.has("original_ench"))
        original_ench.from_json(json["original_ench"]);
    if (json.has("target_item"))
        target_item.from_json(json["target_item"]);

    // available_items
    if (json.has("available_items")) {
        auto items_arr = json["available_items"].as_array();
        available_items.clear();
        available_items.reserve(items_arr.size());
        for (const auto& elem : items_arr) {
            Item item;
            item.from_json(elem);
            available_items.push_back(std::move(item));
        }
    }

    if (json.has("total_exp_level_cost"))
        total_exp_level_cost = static_cast<int32_t>(json["total_exp_level_cost"].as<int64_t>());
    if (json.has("total_exp_cost"))
        total_exp_cost = static_cast<int32_t>(json["total_exp_cost"].as<int64_t>());

    // steps
    if (json.has("steps")) {
        auto steps_arr = json["steps"].as_array();
        steps.clear();
        steps.reserve(steps_arr.size());
        for (const auto& elem : steps_arr) {
            EnchStep step;
            step.from_json(elem);
            steps.push_back(std::move(step));
        }
    }

    if (json.has("max_cost_step_index"))
        max_cost_step_index = static_cast<size_t>(json["max_cost_step_index"].as<int64_t>());
    if (json.has("is_success"))
        is_success = json["is_success"].as<bool>();
}

bool Solution::is_feasible() const { return is_success && steps.size() > 0; }
int32_t Solution::get_peak_level_cost() const {
    if (!is_feasible() || max_cost_step_index >= steps.size() || max_cost_step_index < 0)
        return 0;
    return steps[max_cost_step_index].exp_level_cost;
}
int32_t Solution::get_peak_exp_cost() const {
    if (!is_feasible() || max_cost_step_index >= steps.size() || max_cost_step_index < 0)
        return 0;
    return steps[max_cost_step_index].exp_cost;
}

Solution Solution::make(
    MCE platform, const EnchSet &original_ench, const Item &target_item,
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
    return Solution(
        std::move(meta_data),
        platform,
        original_ench,
        target_item,
        available_items,
        total_exp_level_cost,
        total_exp_cost,
        steps,
        max_cost_step_index,
        is_valid
    );
}
