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
// Stage 1: Business → Algorithm type conversion + ItemResolver
// ====================================================================

/// Convert a business EnchSet (int32_t IDs) to algorithm EnchSet (int16_t IDs).
/// Business IDs are small (< 256 for vanilla), so narrowing is safe.
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
    dst.type = src.is_book() ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
    dst.dur  = static_cast<int16_t>(src.durability);
    dst.ppn  = static_cast<uint8_t>(src.prior_penalty);
    dst.enchs = to_algo_enchset(src.enchantments);
    return dst;
}

algorithm::ResolvedInput detail::SolvePipeline::resolve(
    const SolveInput& input,
    const EnchantmentRegistry& /*ench_reg*/,
    const EquipmentRegistry& /*eq_reg*/)
{
    // Convert business types to algorithm types (IDs remain as-is, will be
    // remapped by CompactAdapter::apply() later).
    auto algo_target = to_algo_item(input.target_item);
    auto algo_source = to_algo_enchset(input.source_enchantments);
    auto algo_desired = to_algo_enchset(input.target_item.enchantments);

    if (!input.is_inventory_mode) {
        // Direct mode: let ItemResolver compute diff and generate books.
        return algorithm::ItemResolver::resolve(algo_target, algo_source, algo_desired);
    }

    // Inventory mode: use pre-resolved extra items directly.
    algorithm::ItemCollection algo_items;
    algo_items.reserve(input.extra_items.size());
    for (const auto& item : input.extra_items)
        algo_items.push_back(to_algo_item(item));

    return algorithm::ResolvedInput{
        algo_target, algo_source, algo_desired, std::move(algo_items)
    };
}

// ====================================================================
// Stage 2: CompactAdapter::apply (domain → compact)
// ====================================================================

algorithm::AlgorithmInput detail::SolvePipeline::apply(
    const algorithm::ResolvedInput& resolved,
    const ::Equipment& target_equipment,
    const EnchantmentRegistry& ench_reg)
{
    return CompactAdapter::apply(resolved, target_equipment, ench_reg);
}

// ====================================================================
// Stage 3: Algorithm execution
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
    if (!(algo->supported_mode() & algo_input.mode)) {
        std::string mode_str =
            (algo_input.mode == algorithm::AlgorithmMode::inventory) ? "inventory" : "direct";
        throw std::runtime_error("Algorithm '" + algorithm +
            "' does not support '" + mode_str + "' mode");
    }

    ExecuteResult exec_result;
    exec_result.algorithm_name = algorithm;

    // Feasibility pre-check
    if (algo_input.mode == algorithm::AlgorithmMode::inventory && !algo->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable from given items");
        return exec_result;
    }

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
// Stage 4: CompactAdapter::recall (compact → business)
// ====================================================================

SolveResult detail::SolvePipeline::recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& algo_input,
    const algorithm::ResolvedInput& resolved)
{
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    // Build a business EnchSet for original_ench from resolved source
    EnchSet original_ench;
    for (const auto& e : resolved.source_ench)
        original_ench.emplace(e.id, e.level);

    // Build business Item for target_item
    Item target_item;
    if (resolved.target_item.type == algorithm::ItemType::Book) {
        target_item = Item(
            to_biz_enchset(resolved.target_item.enchs),
            resolved.target_item.ppn
        );
    } else {
        target_item = Item(
            ::Equipment{"", "", 0, resolved.target_item.dur > 0 ? resolved.target_item.dur : -1},
            to_biz_enchset(resolved.target_item.enchs),
            resolved.target_item.ppn,
            resolved.target_item.dur
        );
    }

    // Build business ItemCollection from resolved available items
    ItemCollection biz_items;
    biz_items.reserve(resolved.available_items.size());
    for (const auto& item : resolved.available_items) {
        if (item.type == algorithm::ItemType::Book)
            biz_items.emplace_back(to_biz_enchset(item.enchs), item.ppn);
        else
            biz_items.emplace_back(
                ::Equipment{"", "", 0, item.dur > 0 ? item.dur : -1},
                to_biz_enchset(item.enchs), item.ppn, item.dur
            );
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
    const EquipmentRegistry& eq_reg,
    const EquipmentCategoryRegistry& /*cat_reg*/)
{
    // Stage 1: Resolve (convert types + compute diff + books)
    auto resolved = resolve(input, ench_reg, eq_reg);

    // Stage 2: Apply (domain → compact)
    // Use the target item's equipment descriptor for EnchReg construction.
    auto algo_input = apply(resolved,
        input.target_item.equipment.has_value()
            ? *input.target_item.equipment
            : ::Equipment{},
        ench_reg);

    // Wire up config / search / mode from SolveInput
    algo_input.config = input.forge_config;
    algo_input.search = input.search_config;
    algo_input.mode = input.is_inventory_mode
        ? algorithm::AlgorithmMode::inventory
        : algorithm::AlgorithmMode::direct;

    // Stage 3: Execute compact algorithm
    auto exec_result = execute(algo_input, input.algorithm, loader);

    // Stage 4: Recall (compact → domain)
    SolveResult result;
    result.algorithm_used = exec_result.algorithm_name;
    result.computation_time_ms = exec_result.computation_time_ms;

    if (exec_result.algo_output.is_valid) {
        result = recall(exec_result.algo_output, algo_input, resolved);
        result.algorithm_used = exec_result.algorithm_name;
        result.computation_time_ms = exec_result.computation_time_ms;
    }

    // Flush diagnostics
    algorithm::DiagnosticsService::instance().flush();

    return result;
}
