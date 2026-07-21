#include "api/SolvePipeline.h"
#include "adapters/CompactAdapter.h"
#include "adapters/OutputFormatter.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/strategies/Strategies.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "resolvers/ItemResolver.h"
#include "types/AlgorithmTypes.h"
#include "log/log.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

// ====================================================================
// Anonymous helpers
// ====================================================================
namespace {

void register_builtin_algorithms(AlgorithmRegistry& registry) {
    registry.register_algorithm("greedy", [] { return std::make_unique<GreedyAlgorithm>(); });
    registry.register_algorithm("dfs", [] { return std::make_unique<DFSAlgorithm>(); });
    registry.register_algorithm("astar", [] { return std::make_unique<AStarAlgorithm>(); });
    registry.register_algorithm("penalty_balance",
                                [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
    registry.register_algorithm("hierarchical",
                                [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
    registry.register_algorithm("idastar",
                                [] { return std::make_unique<IDAStarAlgorithm>(); });
    registry.register_algorithm("hamming",
                                [] { return std::make_unique<HammingAlgorithm>(); });
    registry.register_algorithm("difficulty_first",
                                [] { return std::make_unique<DiffFirstAlgorithm>(); });
    registry.register_algorithm("diff_first",
                                [] { return std::make_unique<DiffFirstAlgorithm>(); });
}

/// Create the requested algorithm via the builtin registry.
/// Throws std::runtime_error if the name is unknown.
std::unique_ptr<IAlgorithm> create_algorithm(const std::string& name) {
    AlgorithmRegistry reg;
    register_builtin_algorithms(reg);
    auto algo = reg.create(name);
    if (!algo) {
        auto available = reg.list();
        std::string msg = "Unknown algorithm: '" + name + "'. Available: ";
        for (size_t i = 0; i < available.size(); ++i) {
            if (i > 0) msg += ", ";
            msg += available[i];
        }
        throw std::runtime_error(msg);
    }
    return algo;
}

} // anonymous namespace

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

// ====================================================================
// SolvePipeline stage methods
// ====================================================================

ResolvedInput detail::SolvePipeline::resolve(
    const SolveInput& input,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& /*eq_reg*/)
{
    if (!input.is_inventory_mode) {
        // Direct mode: ItemResolver validates and generates graduated books
        return ItemResolver::resolve(
            input.target_item,
            input.source_enchantments,
            input.target_item.enchantments,
            ench_reg
        );
    }

    // Inventory mode: extra_items have already been resolved externally
    // (e.g. by InventoryResolver).  Use them directly as available items.
    ResolvedInput resolved{
        input.target_item,
        input.source_enchantments,
        input.target_item.enchantments,
        input.extra_items
    };
    return resolved;
}

AlgorithmInput detail::SolvePipeline::apply(
    const ResolvedInput& resolved,
    const EnchantmentRegistry& ench_reg)
{
    return CompactAdapter::apply(resolved, ench_reg);
}

detail::ExecuteResult detail::SolvePipeline::execute(
    AlgorithmInput& algo_input,
    const std::string& algorithm)
{
    auto algo = create_algorithm(algorithm);

    // Check mode support
    if (!(algo->supported_mode() & algo_input.mode)) {
        std::string mode_str =
            (algo_input.mode == AlgorithmMode::inventory) ? "inventory" : "direct";
        throw std::runtime_error("Algorithm '" + algorithm +
            "' does not support '" + mode_str + "' mode");
    }

    ExecuteResult exec_result;
    exec_result.algorithm_name = algorithm;

    // Feasibility pre-check (meaningful for inventory mode)
    if (algo_input.mode == AlgorithmMode::inventory && !algo->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable from given items");
        return exec_result;  // exec_result.algo_output.is_valid defaults to false
    }

    auto start_time = std::chrono::steady_clock::now();

    AlgorithmExecutor executor(std::move(algo));
    executor.start(algo_input);
    executor.wait();

    auto end_time = std::chrono::steady_clock::now();
    exec_result.computation_time_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(end_time - start_time).count();

    exec_result.algo_output = executor.output();
    return exec_result;
}

SolveResult detail::SolvePipeline::recall(
    const AlgorithmOutput& output,
    const AlgorithmInput& algo_input,
    const ResolvedInput& resolved)
{
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    result.solutions = CompactAdapter::recall(
        output, algo_input,
        resolved.source_ench,
        resolved.target_item,
        resolved.available_items
    );

    result.success = !result.solutions.empty();
    return result;
}

SolveResult detail::SolvePipeline::run(
    const SolveInput& input,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg,
    const EquipmentCategoryRegistry& /*cat_reg*/)
{
    // Stage 1: Resolve (domain validation + book generation)
    auto resolved = resolve(input, ench_reg, eq_reg);

    // Stage 2: Apply (domain -> compact conversion)
    auto algo_input = apply(resolved, ench_reg);

    // Wire up config / search / mode from SolveInput
    algo_input.config = input.forge_config;
    algo_input.search = input.search_config;
    algo_input.mode = input.is_inventory_mode
        ? AlgorithmMode::inventory
        : AlgorithmMode::direct;

    // Stage 3: Execute compact algorithm
    auto exec_result = execute(algo_input, input.algorithm);

    // Stage 4: Recall (compact -> domain conversion)
    SolveResult result;
    result.algorithm_used = exec_result.algorithm_name;
    result.computation_time_ms = exec_result.computation_time_ms;

    if (exec_result.algo_output.is_valid) {
        result = recall(exec_result.algo_output, algo_input, resolved);
        result.algorithm_used = exec_result.algorithm_name;
        result.computation_time_ms = exec_result.computation_time_ms;
    }

    // Flush diagnostics
    DiagnosticsService::instance().flush();

    return result;
}
