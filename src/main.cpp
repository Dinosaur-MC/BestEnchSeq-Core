#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancing.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/HierarchicalMergeStrategy.h"

#include "adapters/CompactAdapter.h"
#include "parser/CLIParser.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/InputParser.h"
#include "parser/OutputFormatter.h"
#include "parser/TagResolver.h"
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

void load_builtin_data(const std::filesystem::path &builtin_data_dir, TagResolver &tag_resolver) {
    EquipmentCategoryRegistry::get_instance().initialize();

    auto ench_infos = EnchInfoParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    auto equipments = EquipmentParser::parse(builtin_data_dir / "vanilla.json", tag_resolver);
    EquipmentRegistry::get_instance().initialize(equipments);
}

void load_custom_data(const std::filesystem::path &data_pack_dir, TagResolver &tag_resolver) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    tag_resolver.load_from(data_pack_dir);
    auto ench_infos = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto existing_ench = EnchantmentRegistry::get_instance().get_instances();
    std::vector<EnchInfo> combined_ench;
    combined_ench.reserve(existing_ench.size() + ench_infos.size());
    for (const auto &info : existing_ench)
        combined_ench.push_back(info);
    for (const auto &info : ench_infos)
        combined_ench.push_back(info);
    EnchantmentRegistry::get_instance().initialize(combined_ench);

    auto equipments = EquipmentParser::parse(data_pack_dir, tag_resolver);
    auto &existing_eq = EquipmentRegistry::get_instance().get_instances();
    std::vector<Equipment> combined_eq;
    combined_eq.reserve(existing_eq.size() + equipments.size());
    for (const auto &eq : existing_eq)
        combined_eq.push_back(eq);
    for (const auto &eq : equipments)
        combined_eq.push_back(eq);
    EquipmentRegistry::get_instance().initialize(combined_eq);
}

void register_builtin_algorithms() {
    AlgorithmRegistry::get_instance().register_algorithm("greedy", [] { return std::make_unique<GreedyAlgorithm>(); });
    AlgorithmRegistry::get_instance().register_algorithm("dfs", [] { return std::make_unique<DFSAlgorithm>(); });
    AlgorithmRegistry::get_instance().register_algorithm("astar", [] { return std::make_unique<AStarAlgorithm>(); });
    AlgorithmRegistry::get_instance().register_algorithm("penalty_balance",
                                                         [] { return std::make_unique<DynamicPenaltyBalancing>(); });
    AlgorithmRegistry::get_instance().register_algorithm("hierarchical",
                                                         [] { return std::make_unique<HierarchicalMergeStrategy>(); });
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
        for (const auto &eq : EquipmentRegistry::get_instance().get_instances()) {
            equipment_map[eq.name_id] = &eq;
        }

        auto parsed = InputParser::assemble_input(config, equipment_map);

        // Register and create algorithm
        register_builtin_algorithms();
        auto algo = AlgorithmRegistry::get_instance().create(config.algorithm);
        if (!algo) {
            auto available = AlgorithmRegistry::get_instance().available_algorithms();
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
                                                  forge_config, EnchantmentRegistry::get_instance());

        // Execute (compact-only algorithm layer)
        AlgorithmExecutor executor(std::move(algo));
        executor.start(algo_input); // copy — keeps algo_input valid for recall() below
        executor.wait();

        // ── Boundary: compact → domain ────────────────────────────────────
        auto solutions =
            adapter.recall(executor.output(), algo_input, parsed.original_ench, parsed.target_item, parsed.available_items);

        // Format output
        std::string output_text;
        if (config.format == "json") {
            output_text = OutputFormatter::format_json(solutions, config.mode);
        } else if (config.format == "compact") {
            output_text = OutputFormatter::format_compact(solutions, config.mode);
        } else {
            output_text = OutputFormatter::format_verbose(solutions, config.mode);
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
