#include "registries/AlgorithmRegistry.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancing.h"
#include "algorithm/strategies/HierarchicalMergeStrategy.h"
#include "utils/SolutionFactory.hpp"
#include "utils/CompactAdapter.hpp"
#include "parser/CLIParser.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/InputParser.h"
#include "parser/OutputFormatter.h"
#include "parser/TagResolver.h"
#include "registries/CompactedRegistries.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/PlatformConfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {

const std::filesystem::path BUILTIN_DATA_DIR = std::filesystem::path("data") / "builtin";

void load_builtin_data(TagResolver& tag_resolver) {
    EquipmentCategoryRegistry::get_instance().initialize();

    auto ench_infos = EnchInfoParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    auto equipments = EquipmentParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    EquipmentRegistry::get_instance().initialize(equipments);
}

void load_custom_data(const std::filesystem::path& data_pack_dir, TagResolver& tag_resolver) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    tag_resolver.load_from(data_pack_dir);
    auto ench_infos = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto existing_ench = EnchantmentRegistry::get_instance().get_instances();
    std::vector<EnchInfo> combined_ench;
    combined_ench.reserve(existing_ench.size() + ench_infos.size());
    for (const auto &info : existing_ench) combined_ench.push_back(info);
    for (const auto &info : ench_infos) combined_ench.push_back(info);
    EnchantmentRegistry::get_instance().initialize(combined_ench);

    auto equipments = EquipmentParser::parse(data_pack_dir, tag_resolver);
    auto& existing_eq = EquipmentRegistry::get_instance().get_instances();
    std::vector<Equipment> combined_eq;
    combined_eq.reserve(existing_eq.size() + equipments.size());
    for (const auto &eq : existing_eq) combined_eq.push_back(eq);
    for (const auto &eq : equipments) combined_eq.push_back(eq);
    EquipmentRegistry::get_instance().initialize(combined_eq);
}

void register_builtin_algorithms() {
    AlgorithmRegistry::get_instance().register_algorithm("greedy", []{
        return std::make_unique<GreedyAlgorithm>();
    });
    AlgorithmRegistry::get_instance().register_algorithm("dfs", []{
        return std::make_unique<DFSAlgorithm>();
    });
    AlgorithmRegistry::get_instance().register_algorithm("astar", []{
        return std::make_unique<AStarAlgorithm>();
    });
    AlgorithmRegistry::get_instance().register_algorithm("penalty_balance", []{
        return std::make_unique<DynamicPenaltyBalancing>();
    });
    AlgorithmRegistry::get_instance().register_algorithm("hierarchical", []{
        return std::make_unique<HierarchicalMergeStrategy>();
    });
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
        load_builtin_data(tag_resolver);

        if (config.data_pack) {
            load_custom_data(std::filesystem::path(*config.data_pack), tag_resolver);
        }

        if (config.platform == "bedrock") {
            platform::Config::get_instance().set_active(platform::MCE::Bedrock);
        } else {
            platform::Config::get_instance().set_active(platform::MCE::Java);
        }

        std::unordered_map<std::string, int32_t> ench_name_to_id;
        for (const auto &info : EnchantmentRegistry::get_instance().get_instances()) {
            int32_t id = EnchantmentRegistry::get_instance().get_id(info.name_id);
            ench_name_to_id[info.name_id] = id;
            if (info.name_id.find(':') == std::string::npos) {
                ench_name_to_id["minecraft:" + info.name_id] = id;
            }
        }

        std::unordered_map<std::string, const Equipment*> equipment_map;
        for (const auto& eq : EquipmentRegistry::get_instance().get_instances()) {
            equipment_map[eq.name_id] = &eq;
        }

        auto parsed = InputParser::assemble_input(config, equipment_map, ench_name_to_id);

        // Register and create algorithm
        register_builtin_algorithms();
        auto algo = AlgorithmRegistry::get_instance().create(config.algorithm);
        if (!algo) {
            throw std::runtime_error("Unknown algorithm: '" + config.algorithm +
                "'. Available: greedy, dfs, astar, penalty_balance, hierarchical");
        }

        //── Boundary: domain → compact ─────────────────────────────────────
        compact::EnchReg ench_reg;
        ench_reg.init(EnchantmentRegistry::get_instance(), *parsed.target_item.equipment);

        AlgorithmInput algo_input;
        algo_input.platform = parsed.platform;
        algo_input.equipment = *parsed.target_item.equipment;
        algo_input.ench_reg = std::move(ench_reg);

        const Equipment* out_eq = parsed.target_item.equipment;
        ItemStack start_item(out_eq, parsed.original_ench, 0);
        algo_input.items.reserve(1 + parsed.available_items.size());
        algo_input.items.push_back(compact::from_domain(start_item, ench_reg));
        for (const auto& book : parsed.available_items)
            algo_input.items.push_back(compact::from_domain(book, ench_reg));

        algo_input.target.reserve(parsed.target_item.enchantments.size());
        for (const auto& e : parsed.target_item.enchantments)
            algo_input.target.push_back({static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});

        // Execute (compact-only algorithm layer)
        AlgorithmExecutor executor(std::move(algo));
        executor.start(std::move(algo_input));
        executor.wait();

        //── Boundary: compact → domain ────────────────────────────────────
        AlgorithmOutput compact_out = executor.output();

        // Assemble solutions: convert each compact step list to domain
        std::vector<EnchSolution> solutions;
        if (compact_out.is_valid) {
            solutions.reserve(compact_out.steps.size());
            for (const auto& step_list : compact_out.steps) {
                auto domain_steps = compact::to_domain(
                    step_list.begin(), step_list.end(), out_eq);
                solutions.push_back(
                    SolutionFactory::create_single(
                        parsed.platform,
                        parsed.original_ench,
                        parsed.target_item,
                        parsed.available_items,
                        domain_steps,
                        compact_out.algorithm_name,
                        compact_out.algorithm_version,
                        compact_out.is_valid
                    )
                );
            }
        }

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
