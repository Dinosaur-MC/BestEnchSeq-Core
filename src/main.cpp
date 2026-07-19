#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/strategies/Strategies.h"
#include "adapters/CompactAdapter.h"
#include "adapters/RawTypeAdapter.h"
#include "cli/cli.h"
#include "config/AppConfig.h"
#include "data/DataLoader.h"
#include "data/EmbeddedData.h"
#include "parsers/EnchInfoParser.h"
#include "log/log.hpp"
#include "adapters/OutputFormatter.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "parsers/ParserUtilsDomain.hpp"
#include "resolvers/InventoryResolver.h"
#include "resolvers/ItemResolver.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "io/json.h"
#include "utils/ParserUtils.hpp"
#include "types/AlgorithmTypes.h"


#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {

/// Load all builtin + custom data as raw, then initialize registries once.
/// This avoids calling RegistryResolver directly — all raw→domain
/// conversion goes through RawTypeAdapter.
void load_all_data(
    const CLIConfig& config,
    EquipmentCategoryRegistry& cat_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg
) {
    // Load builtin data as raw (from embedded JSON — always available)
    std::vector<RawEnchantment> all_ench;
    std::vector<RawEquipment> all_eq;

    // Load builtin (from embedded string — always available)
    {
        auto json = std::string{besq::data::vanilla_json()};
        auto [ench, eq] = EnchInfoParser::parse_native_json_str(json);
        all_ench = std::move(ench);
        all_eq = std::move(eq);
    }

    // Step 2: If custom data pack, merge raw data
    if (config.data_pack) {
        auto dp = std::filesystem::path(*config.data_pack);
        if (!std::filesystem::exists(dp))
            throw std::runtime_error("Data pack directory not found: " + dp.string());

        auto [custom_ench, custom_eq] = EnchInfoParser::parse(dp);
        all_ench.insert(all_ench.end(), custom_ench.begin(), custom_ench.end());
        all_eq.insert(all_eq.end(), custom_eq.begin(), custom_eq.end());
    }

    // Step 3: Initialize ALL registries from merged raw data
    RawTypeAdapter::resolve(all_ench, all_eq, cat_reg, eq_reg, ench_reg);
}

void register_builtin_algorithms(AlgorithmRegistry &registry) {
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
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    try {
        // Load environment-backed config (CLI overrides take precedence later)
        auto app_cfg = AppConfig::load();
        auto config = parse_cli(argc, argv);

        // ── Configure global Logger ─────────────────────────────────────────
        Logger::instance().set_level(
            app_cfg.log_level >= 3 ? LogLevel::Error
          : app_cfg.log_level >= 2 ? LogLevel::Warn
          : app_cfg.log_level >= 1 ? LogLevel::Info
          :                          LogLevel::Debug);
        Logger::instance().set_retention(app_cfg.log_retention);

        if (config.help || config.version) {
            return 0;
        }

        // ── Load data ────────────────────────────────────────────────────────
        EquipmentCategoryRegistry cat_reg;
        EnchantmentRegistry ench_reg;
        EquipmentRegistry eq_reg;
        load_all_data(config, cat_reg, ench_reg, eq_reg);

        // ── Resolve CLI specs → domain items ─────────────────────────────
        auto target_spec = ItemParser::parse(config.target);
        auto target_item = build_target(target_spec, ench_reg, eq_reg);

        EnchSet source_ench;
        if (!config.source.empty()) {
            auto source_specs = EnchParser::parse(config.source);
            source_ench = build_enchset(source_specs, ench_reg);
        }

        EnchSet target_ench;
        if (!target_spec.inline_enchants.empty()) {
            target_ench = build_enchset(target_spec.inline_enchants, ench_reg);
        }

        // ── Register and create algorithm (local registry, no singleton) ──
        AlgorithmRegistry algo_reg;
        register_builtin_algorithms(algo_reg);
        auto algo = algo_reg.create(config.algorithm);
        if (!algo) {
            auto available = algo_reg.list();
            std::string msg = "Unknown algorithm: '" + config.algorithm + "'. Available: ";
            for (size_t i = 0; i < available.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += available[i];
            }
            throw std::runtime_error(msg);
        }

        // Check algorithm mode support
        AlgorithmMode algo_mode = (config.mode == "inventory")
            ? AlgorithmMode::inventory
            : AlgorithmMode::direct;
        if (!(algo->supported_mode() & algo_mode)) {
            throw std::runtime_error("Algorithm '" + config.algorithm +
                "' does not support '" + config.mode + "' mode");
        }

        // ── Boundary: domain → compact ────────────────────────────────────
        // CompactAdapter is fully static — no instance needed
        ForgeConfig forge_config;
        if (config.platform == "auto") {
            forge_config.platform = MCE::All;
        } else {
            forge_config.platform = ParserUtils::parse_platform(config.platform);
        }

        AlgorithmInput algo_input;
        // Keep these alive for the recall call later
        EnchSet recall_source_ench;
        ItemStack recall_target_item;
        ItemCollection recall_available_items;

        if (config.mode == "direct") {
            auto resolved = ItemResolver::resolve(
                target_item, source_ench, target_ench, ench_reg);
            recall_source_ench = resolved.source_ench;
            recall_target_item = resolved.target_item;
            recall_available_items = resolved.available_items;
            algo_input = CompactAdapter::apply(resolved, ench_reg);
        } else {
            // inventory mode
            if (!config.input.has_value())
                throw std::runtime_error("Input file required for inventory mode");

            auto inv = InventoryResolver::resolve(
                std::filesystem::path(*config.input), ench_reg, eq_reg);

            for (const auto& w : inv.warnings)
                LOG_WARN("Inventory: %s", w.c_str());

            ResolvedInput resolved{
                target_item, source_ench, target_ench, std::move(inv.items)};
            recall_source_ench = resolved.source_ench;
            recall_target_item = resolved.target_item;
            recall_available_items = resolved.available_items;
            algo_input = CompactAdapter::apply(resolved, ench_reg);
        }

        algo_input.config.platform = forge_config.platform;
        algo_input.mode = algo_mode;

        // ── Search config from CLI ────────────────────────────────────────
        algo_input.search.max_solutions = config.solutions;
        algo_input.search.memory_mb = static_cast<int32_t>(
            config.memory_mb > 0 ? config.memory_mb : app_cfg.memory_mb);

        // ── Feasibility pre-check (inventory mode) ─────────────────────────
        bool feasible = true;
        if (config.mode == "inventory" && !algo->simulate(algo_input)) {
            LOG_INFO("simulate: target not reachable from given items");
            feasible = false;
        }

        LOG_INFO("Starting algorithm: %s", config.algorithm.c_str());

        // Execute (compact-only algorithm layer)
        std::vector<EnchSolution> solutions;
        if (feasible) {
            AlgorithmExecutor executor(std::move(algo));
            executor.start(algo_input);
            executor.wait();

            // ── Verbose diagnostics ────────────────────────────────────────
            if (config.verbose || app_cfg.verbose) {
                auto diag = executor.get_diagnostics(0);
                LOG_INFO(
                    "nodes_visited=%lld  nodes_pruned=%lld  steps_forged=%lld  progress=%.1f%%",
                    diag.nodes_visited, diag.nodes_pruned, diag.steps_forged,
                    diag.progress * 100.0);
            }

            // ── Boundary: compact → domain ────────────────────────────────
            solutions = CompactAdapter::recall(executor.output(), algo_input,
                                        recall_source_ench, recall_target_item,
                                        recall_available_items);
        }

        // Format output
        std::string output_text;
        if (config.format == "json") {
            output_text = OutputFormatter::format_json(solutions, ench_reg, cat_reg, config.mode);
        } else if (config.format == "compact") {
            output_text = OutputFormatter::format_compact(solutions, ench_reg, cat_reg, config.mode);
        } else {
            output_text = OutputFormatter::format_verbose(solutions, ench_reg, cat_reg, config.mode);
        }

        if (config.output) {
            std::ofstream out(*config.output);
            if (!out.is_open()) {
                throw std::runtime_error("Failed to open output file: " + *config.output);
            }
            out << output_text;
        } else {
            std::cout << output_text;
        }

        DiagnosticsService::instance().flush();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
