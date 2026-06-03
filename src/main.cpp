#include "algorithm/BaseAlgorithm.h"
#include "parser/CLIParser.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/InputParser.h"
#include "parser/OutputFormatter.h"
#include "parser/TagResolver.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

const std::filesystem::path BUILTIN_DATA_DIR = std::filesystem::path("data") / "builtin";

struct Registry {
    std::unordered_map<std::string, const EquipmentType*> equipment_map;
    std::vector<EquipmentType> equipment_list;

    void init_equipment(const std::vector<EquipmentType> &eq_list) {
        equipment_list.clear();
        equipment_list.reserve(eq_list.size());
        for (const auto &eq : eq_list) {
            equipment_list.push_back(eq);
        }
        equipment_map.clear();
        for (auto &eq : equipment_list) {
            equipment_map[eq.id] = &eq;
        }
    }
};

Registry load_builtin_data(TagResolver &tag_resolver) {
    Registry registry;
    auto ench_infos = EnchInfoParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    EnchInfo::initialize(ench_infos);

    auto equipments = EquipmentParser::parse(BUILTIN_DATA_DIR / "vanilla.json", tag_resolver);
    registry.init_equipment(equipments);
    return registry;
}

void load_custom_data(
    const std::filesystem::path &data_pack_dir,
    TagResolver &tag_resolver,
    Registry &registry
) {
    if (!std::filesystem::exists(data_pack_dir)) {
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());
    }

    tag_resolver.load_from(data_pack_dir);

    auto ench_infos = EnchInfoParser::parse(data_pack_dir, tag_resolver);
    auto existing = EnchInfo::get_instances();
    std::vector<EnchInfo> combined;
    combined.reserve(existing.size() + ench_infos.size());
    for (const auto &info : existing) combined.push_back(info);
    for (const auto &info : ench_infos) combined.push_back(info);
    EnchInfo::initialize(combined);

    auto equipments = EquipmentParser::parse(data_pack_dir, tag_resolver);
    auto &eq_list = registry.equipment_list;
    eq_list.reserve(eq_list.size() + equipments.size());
    for (const auto &eq : equipments) eq_list.push_back(eq);
    registry.init_equipment(eq_list);
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
        Registry registry = load_builtin_data(tag_resolver);

        if (config.data_pack) {
            load_custom_data(std::filesystem::path(*config.data_pack), tag_resolver, registry);
        }

        if (config.platform == "bedrock") {
            EnchInfo::set_active_platform(platform::MCE::Bedrock);
        } else if (config.platform == "java") {
            EnchInfo::set_active_platform(platform::MCE::Java);
        } else {
            EnchInfo::set_active_platform(platform::MCE::Java);
        }

        std::unordered_map<std::string, int32_t> ench_name_to_id;
        for (const auto &info : EnchInfo::get_instances()) {
            int32_t id = EnchInfo::get_id(info.name_id);
            ench_name_to_id[info.name_id] = id;
            if (info.name_id.find(':') == std::string::npos) {
                ench_name_to_id["minecraft:" + info.name_id] = id;
            }
        }

        auto input = InputParser::assemble_input(config, registry.equipment_map, ench_name_to_id);

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
