#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancingAlgorithm.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/HierarchicalMergeAlgorithm.h"

#include "adapters/CompactAdapter.h"
#include "cli.h"
#include "types/AppConfig.h"
#include "parsers/EnchInfoParser.h"
#include "algorithm/strategies/IDAStarAlgorithm.h"
#include "log/log.hpp"
#include "parsers/EquipmentParser.h"
#include "parsers/InputParser.h"
#include "adapters/OutputFormatter.h"
#include "adapters/RegistryResolver.h"
#include "utils/TagResolver.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"


#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {

void load_builtin_data(
    const std::filesystem::path &builtin_data_dir,
    TagResolver &tag_resolver,
    const EquipmentCategoryRegistry &cat_reg,
    EnchantmentRegistry &ench_reg,
    EquipmentRegistry &eq_reg
) {
    auto raw_ench = EnchInfoParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
    ench_reg.initialize(ench_infos);

    auto raw_eq = EquipmentParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
    eq_reg.initialize(equipments);
}

void load_custom_data(
    const std::filesystem::path &data_pack_dir,
    TagResolver &tag_resolver,
    const EquipmentCategoryRegistry &cat_reg,
    EnchantmentRegistry &ench_reg,
    EquipmentRegistry &eq_reg
) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    tag_resolver.load_from(data_pack_dir);

    // Parse new data as raw
    auto raw_ench = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto raw_eq   = EquipmentParser::parse(data_pack_dir, tag_resolver);

    // Merge with already-loaded data and re-initialize
    {
        auto existing = ench_reg.get_instances();
        std::vector<EnchInfo> combined;
        combined.reserve(existing.size() + raw_ench.size());
        for (const auto &info : existing)
            combined.push_back(info);
        auto new_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
        for (const auto &info : new_infos)
            combined.push_back(info);
        ench_reg.initialize(combined);
    }
    {
        auto &existing = eq_reg.get_instances();
        std::vector<Equipment> combined;
        combined.reserve(existing.size() + raw_eq.size());
        for (const auto &eq : existing)
            combined.push_back(eq);
        auto new_eq = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
        for (const auto &eq : new_eq)
            combined.push_back(eq);
        eq_reg.initialize(combined);
    }
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

        if (config.help) {
            return 0;
        }

        TagResolver tag_resolver;

        // ── Load data ────────────────────────────────────────────────────────

        // Resolve builtin data directory: prefer path relative to executable,
        // fall back to CWD-relative path for development builds.
        auto builtin_data_dir = std::filesystem::absolute(argv[0]).parent_path() / "data" / "builtin";
        if (!std::filesystem::exists(builtin_data_dir)) {
            builtin_data_dir = std::filesystem::path("data") / "builtin";
        }

        // Local registries (no globals)
        EquipmentCategoryRegistry cat_reg;
        cat_reg.initialize();

        EnchantmentRegistry ench_reg;
        EquipmentRegistry eq_reg;

        load_builtin_data(builtin_data_dir, tag_resolver, cat_reg, ench_reg, eq_reg);

        if (config.data_pack) {
            load_custom_data(std::filesystem::path(*config.data_pack), tag_resolver,
                             cat_reg, ench_reg, eq_reg);
        }

        // ── Build equipment map for InputParser ──────────────────────────
        std::unordered_map<std::string, const Equipment *> equipment_map;
        for (const auto &eq : eq_reg.get_instances()) {
            equipment_map[eq.name_id] = &eq;
        }

        auto parsed = InputParser::assemble_input(config, ench_reg, equipment_map);

        // Register and create algorithm (local registry, no singleton)
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

        // ── Boundary: domain → compact ─────────────────────────────────────
        CompactAdapter adapter;
        ForgeConfig forge_config;
        forge_config.platform = parsed.platform;
        AlgorithmInput algo_input = adapter.apply(parsed.target_item, parsed.original_ench, parsed.available_items,
                                                  forge_config, ench_reg);

        // ── Memory budget for AStar (CLI → AppConfig → default) ────────────
        {
            int64_t mb = config.memory_mb > 0 ? config.memory_mb : app_cfg.memory_mb;
            auto* astar = dynamic_cast<AStarAlgorithm*>(algo.get());
            if (astar) {
                astar->set_budget(AStarMemoryBudget::from_memory_mb(mb > 0 ? mb : 2048, 0));
            }
        }

        LOG_INFO("Starting algorithm: %s", config.algorithm.c_str());

        // Execute (compact-only algorithm layer)
        AlgorithmExecutor executor(std::move(algo));
        executor.start(algo_input);
        executor.wait();

        // ── Verbose diagnostics ────────────────────────────────────────────
        if (config.verbose || app_cfg.verbose) {
            auto diag = executor.get_diagnostics(0);
            LOG_INFO(
                "nodes_visited=%lld  nodes_pruned=%lld  steps_forged=%lld  progress=%.1f%%",
                diag.nodes_visited, diag.nodes_pruned, diag.steps_forged,
                diag.progress * 100.0);
        }

        // ── Boundary: compact → domain ────────────────────────────────────
        auto solutions =
            adapter.recall(executor.output(), algo_input, parsed.original_ench, parsed.target_item, parsed.available_items);

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

        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
