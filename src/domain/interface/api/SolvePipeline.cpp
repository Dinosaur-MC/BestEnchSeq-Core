#include "SolvePipeline.h"
#include "domain/algorithm/algorithm.h"
#include "domain/business/business.h"
#include "domain/orchestration/orchestration.h"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// ====================================================================
// SolveResult formatting
// ====================================================================

std::string SolveResult::to_json(
    const EnchantmentRegistry& ench_reg,
    const EquipmentCategoryRegistry& cat_reg) const
{
    return OutputFormatter::format_json(solutions, ench_reg, cat_reg, "direct");
}

std::string SolveResult::to_text(
    const EnchantmentRegistry& ench_reg,
    const EquipmentCategoryRegistry& cat_reg) const
{
    return OutputFormatter::format_verbose(solutions, ench_reg, cat_reg, "direct");
}

std::string SolveResult::to_json_raw() const {
    Json::Object root;
    root["success"] = Json(Json::Bool(success));
    root["algorithm"] = Json(Json::String(algorithm_used));
    root["computation_time_ms"] = Json(Json::Number(static_cast<int64_t>(computation_time_ms)));

    Json::Array sol_arr;
    for (const auto& sol : solutions) {
        Json::Object s;
        s["total_exp_level_cost"] = Json(Json::Number(sol.total_exp_level_cost));
        s["total_exp_cost"] = Json(Json::Number(sol.total_exp_cost));
        s["is_success"] = Json(Json::Bool(sol.is_success));
        s["step_count"] = Json(Json::Number(static_cast<int32_t>(sol.steps.size())));
        sol_arr.push_back(Json(s));
    }
    root["solutions"] = Json(sol_arr);
    return Json(root).to_string(Json::Pretty);
}

// ====================================================================
// Type conversion helpers
// ====================================================================

/// Convert a business EnchSet (int32_t IDs) to algorithm EnchSet (int16_t IDs).
static algorithm::EnchSet to_algo_enchset(const EnchSet& src) {
    algorithm::EnchSet dst;
    for (const auto& e : src)
        dst.insert(algorithm::Ench{static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});
    return dst;
}

/// Convert an algorithm EnchSet (int16_t IDs) back to business EnchSet (int32_t IDs).
static EnchSet to_biz_enchset(const algorithm::EnchSet& src) {
    EnchSet dst;
    for (const auto& e : src)
        dst.emplace(e.id, e.level);
    return dst;
}

/// Convert a business Item to algorithm Item (without EnchReg remapping).
static algorithm::Item to_algo_item(const Item& src) {
    algorithm::Item dst;
    dst.type  = src.is_book() ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
    dst.dur   = static_cast<int16_t>(src.durability);
    dst.ppn   = static_cast<uint8_t>(src.prior_penalty);
    dst.enchs = to_algo_enchset(src.enchantments);
    return dst;
}

// ====================================================================
// Stage 1: CompactAdapter::apply (domain → compact, no books yet)
// ====================================================================

algorithm::AlgorithmInput detail::SolvePipeline::apply(
    const SolveInput& input,
    const ::Equipment& target_equipment,
    const EnchantmentRegistry& ench_reg)
{
    // Build the target Item (with desired enchantments in algorithm ID space)
    auto algo_target = to_algo_item(input.target_item);

    // Build source enchantments (also converted to algorithm IDs)
    auto algo_source = to_algo_enchset(input.source_enchantments);

    // Build AlgorithmInput via CompactAdapter
    auto algo_input = CompactAdapter::apply(
        algo_target, algo_source, target_equipment, ench_reg);

    // Wire up config / search / mode
    algo_input.f_config = input.forge_config;
    algo_input.s_config = input.search_config;
    algo_input.mode = input.is_inventory_mode
        ? AlgorithmMode::inventory
        : AlgorithmMode::direct;
    algo_input.priorities = input.extra_item_priorities;

    // Fill the union: direct mode → source enchants; inventory → available items
    if (!input.is_inventory_mode) {
        // Convert EnchSet to EnchCollection (variant expects vector<Ench>)
        algorithm::EnchCollection src_vec;
        src_vec.reserve(algo_source.size());
        for (const auto& e : algo_source)
            src_vec.push_back(e);
        algo_input.data = std::move(src_vec);
    } else {
        algorithm::ItemCollection extra;
        extra.reserve(input.extra_items.size());
        for (const auto& item : input.extra_items)
            extra.push_back(to_algo_item(item));
        algo_input.data = std::move(extra);
    }

    return algo_input;
}

// ====================================================================
// Stage 2: Create algorithm → execute
// ====================================================================

detail::ExecuteResult detail::SolvePipeline::execute(
    algorithm::AlgorithmInput& algo_input,
    const std::string& algorithm,
    const algorithm::AlgorithmLoader& loader)
{
    auto algo = loader.create(algorithm);
    if (!algo) {
        auto available = loader.list();
        std::string msg = "Unknown algorithm: '" + algorithm + "'. Available: ";
        for (size_t i = 0; i < available.size(); ++i) {
            if (i > 0) msg += ", ";
            msg += available[i];
        }
        throw std::runtime_error(msg);
    }

    // Check mode support
    if (!(algo->supported_mode() == algo_input.mode)) {
        std::string mode_str =
            (algo_input.mode == AlgorithmMode::inventory) ? "inventory" : "direct";
        throw std::runtime_error("Algorithm '" + algorithm +
            "' does not support '" + mode_str + "' mode");
    }

    // Pre-process: resolve generates books (direct) or filters items (inventory)
    auto resolved = algo->resolve(algo_input);

    ExecuteResult exec_result;
    exec_result.algorithm_name = algorithm;

    // Check reachability after resolve
    if (resolved.empty()) {
        if (algo_input.mode == AlgorithmMode::inventory)
            LOG_INFO("resolve: inventory unreachable");
        else
            LOG_INFO("resolve: target already satisfied");
        return exec_result;
    }

    // Append resolved items (books / filtered inventory) to the items list
    size_t old_size = algo_input.items.size();
    algo_input.items.resize(old_size + resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i)
        algo_input.items[old_size + i] = std::move(resolved[i]);

    // Feasibility pre-check
    if (!algo->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable");
        return exec_result;
    }

    // Execute
    auto start_time = std::chrono::steady_clock::now();

    algorithm::AlgorithmExecutor executor(std::move(algo));
    executor.start(algo_input);
    executor.wait();

    auto end_time = std::chrono::steady_clock::now();
    exec_result.computation_time_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(end_time - start_time).count();

    exec_result.algo_output = executor.output();
    return exec_result;
}

// ====================================================================
// Stage 3: CompactAdapter::recall (compact → business)
// ====================================================================

SolveResult detail::SolvePipeline::recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& algo_input,
    const algorithm::EnchSet& original_source_ench,
    const Item& original_target_item)
{
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    // Build a business EnchSet for original_ench from resolved source
    EnchSet original_ench;
    for (const auto& e : original_source_ench)
        original_ench.emplace(e.id, e.level);

    // Build business Item for target_item
    Item target_item;
    if (original_target_item.is_book()) {
        target_item = Item(
            to_biz_enchset(original_source_ench),
            original_target_item.prior_penalty
        );
    } else {
        target_item = Item(
            original_target_item.equipment.value_or(::Equipment{}),
            to_biz_enchset(original_source_ench),
            original_target_item.prior_penalty,
            original_target_item.durability
        );
    }

    // Build business ItemCollection from algorithm input's inventory items
    ItemCollection biz_items;
    if (algo_input.is_inventory()) {
        for (const auto& item : algo_input.inventory_items())
            biz_items.push_back(CompactAdapter::to_domain(item, algo_input.ench_reg));
    }

    result.solutions = CompactAdapter::recall(
        output, algo_input, original_ench, target_item, biz_items
    );
    result.success = !result.solutions.empty();
    return result;
}

// ====================================================================
// Full pipeline
// ====================================================================

SolveResult detail::SolvePipeline::run(
    const SolveInput& input,
    const algorithm::AlgorithmLoader& loader,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& /*eq_reg*/,
    const EquipmentCategoryRegistry& /*cat_reg*/)
{
    // Stage 1: Apply — build AlgorithmInput (equipment + EnchReg, no books yet)
    auto algo_input = apply(input,
        input.target_item.equipment.value_or(::Equipment{}),
        ench_reg);

    // Stage 2: Execute — create algorithm, run resolve, execute search
    auto exec_result = execute(algo_input, input.algorithm, loader);

    // Short-circuit if no output to recall
    if (!exec_result.algo_output.is_valid) {
        SolveResult empty_result;
        empty_result.algorithm_used = exec_result.algorithm_name;
        empty_result.computation_time_ms = exec_result.computation_time_ms;
        return empty_result;
    }

    // Stage 3: Recall — convert back to business types
    algorithm::EnchSet algo_source;
    for (const auto& e : input.source_enchantments)
        algo_source.insert({static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});

    auto result = recall(exec_result.algo_output, algo_input,
                         algo_source, input.target_item);
    result.algorithm_used = exec_result.algorithm_name;
    result.computation_time_ms = exec_result.computation_time_ms;

    return result;
}
