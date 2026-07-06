#include "algorithm/BaseAlgorithm.h"
#include "parser/CLIParser.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/InputParser.h"
#include "parser/OutputFormatter.h"
#include "parser/TagResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/PlatformConfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

const std::filesystem::path BUILTIN_DATA_DIR = std::filesystem::path("data") / "builtin";

void load_builtin_data(TagResolver& tag_resolver) {
    auto ench_infos = EnchInfoParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);

    auto equipments = EquipmentParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    EquipmentRegistry::get_instance().initialize(equipments);
}

void load_custom_data(const std::filesystem::path& data_pack_dir, TagResolver& tag_resolver) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    tag_resolver.load_from(data_pack_dir);

    // Combine custom enchantments with existing
    auto ench_infos = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto existing_ench = EnchantmentRegistry::get_instance().get_instances();
    std::vector<EnchInfo> combined_ench;
    combined_ench.reserve(existing_ench.size() + ench_infos.size());
    for (const auto &info : existing_ench) combined_ench.push_back(info);
    for (const auto &info : ench_infos) combined_ench.push_back(info);
    EnchantmentRegistry::get_instance().initialize(combined_ench);

    // Combine custom equipment with existing
    auto equipments = EquipmentParser::parse(data_pack_dir, tag_resolver);
    auto existing_eq = EquipmentRegistry::get_instance().get_instances();
    std::vector<EquipmentType> combined_eq;
    combined_eq.reserve(existing_eq.size() + equipments.size());
    for (const auto &eq : existing_eq) combined_eq.push_back(eq);
    for (const auto &eq : equipments) combined_eq.push_back(eq);
    EquipmentRegistry::get_instance().initialize(combined_eq);
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

        std::unordered_map<std::string, const EquipmentType*> equipment_map;
        for (const auto& eq : EquipmentRegistry::get_instance().get_instances()) {
            equipment_map[eq.id] = &eq;
        }
        auto input = InputParser::assemble_input(config, equipment_map, ench_name_to_id);

        BaseAlgorithm::Output algo_output;
        algo_output.algorithm_name = "default";
        algo_output.algorithm_version = "0.1.0";
        algo_output.created_at = 0;
        algo_output.computation_time = 0;
        algo_output.steps = {};
        algo_output.is_valid = false;

        auto solutions = Utils::make_solution(input, algo_output);

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
