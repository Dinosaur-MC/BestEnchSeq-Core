#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancingAlgorithm.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/HierarchicalMergeAlgorithm.h"

#include "adapters/CompactAdapter.h"
#include "parsers/CLIParser.h"
#include "parsers/EnchInfoParser.h"
#include "algorithm/strategies/IDAStarAlgorithm.h"
#include "utils/Logger.hpp"
#include "parsers/EquipmentParser.h"
#include "parsers/InputParser.h"
#include "adapters/OutputFormatter.h"
#include "adapters/RegistryResolver.h"
#include "utils/TagResolver.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/RegistryAccess.h"
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

void load_builtin_data(const std::filesystem::path &builtin_data_dir, TagResolver &tag_resolver) {
    registries::categories().initialize();

    auto &cat_reg = registries::categories();
    auto raw_ench = EnchInfoParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
    registries::enchants().initialize(ench_infos);
    auto raw_eq = EquipmentParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
    registries::equipment().initialize(equipments);
}

void load_custom_data(const std::filesystem::path &data_pack_dir, TagResolver &tag_resolver) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    auto &cat_reg = registries::categories();
    tag_resolver.load_from(data_pack_dir);
    auto raw_ench = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
    auto existing_ench = registries::enchants().get_instances();
    std::vector<EnchInfo> combined_ench;
    combined_ench.reserve(existing_ench.size() + ench_infos.size());
    for (const auto &info : existing_ench)
        combined_ench.push_back(info);
    for (const auto &info : ench_infos)
        combined_ench.push_back(info);
    registries::enchants().initialize(combined_ench);

    auto raw_eq = EquipmentParser::parse(data_pack_dir, tag_resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
    auto &existing_eq = registries::equipment().get_instances();
    std::vector<Equipment> combined_eq;
    combined_eq.reserve(existing_eq.size() + equipments.size());
    for (const auto &eq : existing_eq)
        combined_eq.push_back(eq);
    for (const auto &eq : equipments)
        combined_eq.push_back(eq);
    registries::equipment().initialize(combined_eq);
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
        CLIParser cli_parser;
        auto config = cli_parser.parse(argc, argv);

        if (config.help) {
            return 0;
        }

        TagResolver tag_resolver;

        // Resolve builtin data directory: prefer path relative to executable,
        // fall back to CWD-relative path for development builds.
        auto builtin_data_dir = std::filesystem::absolute(argv[0]).parent_path() / "data" / "builtin";
        if (!std::filesystem::exists(builtin_data_dir)) {
            builtin_data_dir = std::filesystem::path("data") / "builtin";
        }
        load_builtin_data(builtin_data_dir, tag_resolver);

        if (config.data_pack) {
            load_custom_data(std::filesystem::path(*config.data_pack), tag_resolver);
        }

        // platform is embedded in ForgeConfig via CompactAdapter::apply() below

        std::unordered_map<std::string, const Equipment *> equipment_map;
        for (const auto &eq : registries::equipment().get_instances()) {
            equipment_map[eq.name_id] = &eq;
        }

        auto parsed = InputParser::assemble_input(config, registries::enchants(), equipment_map);

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
                                                  forge_config, registries::enchants());

        // ── Memory budget for AStar (set before moving algo into executor) ─
        if (config.memory_mb > 0) {
            auto* astar = dynamic_cast<AStarAlgorithm*>(algo.get());
            if (astar) {
                astar->set_budget(AStarMemoryBudget::from_memory_mb(config.memory_mb, 0));
            }
        }

        // ── Logger ────────────────────────────────────────────────────
        Logger logger;
        logger.info("Starting algorithm: " + config.algorithm);

        // Execute (compact-only algorithm layer)
        AlgorithmExecutor executor(std::move(algo));
        executor.start(algo_input); // copy — keeps algo_input valid for recall() below
        executor.wait();

        // ── Verbose diagnostics ────────────────────────────────────────────
        if (config.verbose) {
            auto diag = executor.get_diagnostics(0);
            logger.printf(LogLevel::Info,
                "nodes_visited=%lld  nodes_pruned=%lld  steps_forged=%lld  progress=%.1f%%",
                diag.nodes_visited, diag.nodes_pruned, diag.steps_forged,
                diag.progress * 100.0);
        }

        // ── Boundary: compact → domain ────────────────────────────────────
        auto solutions =
            adapter.recall(executor.output(), algo_input, parsed.original_ench, parsed.target_item, parsed.available_items);

        // Format output
        std::string output_text;
        auto &ench_reg = registries::enchants();
        auto &cat_reg  = registries::categories();
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
