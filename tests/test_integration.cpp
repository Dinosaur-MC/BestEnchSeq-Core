#include "algorithm/BaseAlgorithm.h"
#include "parser/CLIParser.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/InputParser.h"
#include "parser/OutputFormatter.h"
#include "parser/ParserUtils.h"
#include "parser/TagResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"
#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

// ---------------------------------------------------------------------------
// Full pipeline: direct mode with builtin data
// ---------------------------------------------------------------------------
void test_full_pipeline_direct() {
    TagResolver resolver;
    auto ench_infos = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);

    const char *argv[] = {"besq", "--target", "diamond_sword", "--wanted", "sharpness=5,knockback=2"};
    CLIParser cli_parser;
    auto config = cli_parser.parse(5, const_cast<char **>(argv));

    auto equipments = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    std::unordered_map<std::string, const EquipmentType *> eq_map;
    for (auto &eq : equipments) eq_map[eq.id] = &eq;

    std::unordered_map<std::string, int32_t> ench_map;
    for (const auto &info : EnchantmentRegistry::get_instance().get_instances()) {
        int32_t id = EnchantmentRegistry::get_instance().get_id(info.name_id);
        ench_map[info.name_id] = id;
        if (info.name_id.find(':') == std::string::npos) {
            ench_map["minecraft:" + info.name_id] = id;
        }
    }

    auto input = InputParser::assemble_input(config, eq_map, ench_map);

    expect(input.available_items.size() == 2,
           "full_pipeline_direct: auto-complete should generate 2 books");
    expect(input.target_item.equipment != nullptr,
           "full_pipeline_direct: target should have equipment");
    expect(input.target_item.equipment->id == "diamond_sword",
           "full_pipeline_direct: target should be diamond sword");

    std::cout << "  [OK] test_full_pipeline_direct" << std::endl;
}

// ---------------------------------------------------------------------------
// Inventory mode pipeline
// ---------------------------------------------------------------------------
void test_full_pipeline_inventory() {
    TagResolver resolver;
    auto ench_infos = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);

    auto equipments = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    std::unordered_map<std::string, const EquipmentType *> eq_map;
    for (auto &eq : equipments) eq_map[eq.id] = &eq;

    std::unordered_map<std::string, int32_t> ench_map;
    for (const auto &info : EnchantmentRegistry::get_instance().get_instances()) {
        int32_t id = EnchantmentRegistry::get_instance().get_id(info.name_id);
        ench_map[info.name_id] = id;
        if (info.name_id.find(':') == std::string::npos) {
            ench_map["minecraft:" + info.name_id] = id;
        }
    }

    // Write a temp inventory file
    {
        std::ofstream f("test_inv_pipeline.json");
        f << R"({
            "items": [
                {"type": "book", "enchants": [{"id": "sharpness", "level": 5}], "prior_penalty": 0},
                {"type": "book", "enchants": [{"id": "knockback", "level": 2}], "prior_penalty": 0},
                {"type": "equipment", "id": "diamond_sword", "enchants": [], "prior_penalty": 0, "durability": 1561}
            ]
        })";
    }

    const char *argv[] = {"besq", "--mode", "inventory", "--input", "test_inv_pipeline.json",
                          "--target", "diamond_sword", "--wanted", "sharpness=5"};
    CLIParser cli_parser;
    auto config = cli_parser.parse(9, const_cast<char **>(argv));

    auto input = InputParser::assemble_input(config, eq_map, ench_map);

    expect(input.available_items.size() >= 2,
           "full_pipeline_inventory: should have at least 2 items");
    expect(input.target_item.equipment != nullptr,
           "full_pipeline_inventory: target should have equipment");

    std::filesystem::remove("test_inv_pipeline.json");
    std::cout << "  [OK] test_full_pipeline_inventory" << std::endl;
}

// ---------------------------------------------------------------------------
// Platform switching
// ---------------------------------------------------------------------------
void test_platform_switching() {
    TagResolver resolver;
    auto ench_infos = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);

    platform::Config::get_instance().set_active(platform::MCE::Java);
    expect(platform::get_active_platform() == platform::MCE::Java,
           "platform_switch: Java set correctly");

    platform::Config::get_instance().set_active(platform::MCE::Bedrock);
    expect(platform::get_active_platform() == platform::MCE::Bedrock,
           "platform_switch: Bedrock set correctly");

    std::cout << "  [OK] test_platform_switching" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_enchantment_lookup() {
    TagResolver resolver;
    auto ench_infos = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);

    expect(EnchantmentRegistry::get_instance().get_id("sharpness") >= 0, "builtin: sharpness found");
    expect(EnchantmentRegistry::get_instance().get_id("nonexistent") < 0, "builtin: nonexistent not found");

    auto &sharpness = EnchantmentRegistry::get_instance().get("sharpness");
    expect(sharpness.name_id == "sharpness",
           "builtin: sharpness name_id is sharpness");
    expect(sharpness.max_level == 5, "builtin: sharpness max_level is 5");
    expect(sharpness.multiplier == 1, "builtin: sharpness multiplier is 1");

    std::cout << "  [OK] test_builtin_enchantment_lookup" << std::endl;
}

// ---------------------------------------------------------------------------
// Equipment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_equipment_lookup() {
    TagResolver resolver;
    auto equipments = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);

    bool found_sword = false;
    bool found_netherite_helmet = false;
    for (const auto &eq : equipments) {
        if (eq.id == "diamond_sword") {
            found_sword = true;
            expect(eq.category == EquipmentCategory("sword"),
                   "builtin_eq: diamond_sword category is sword");
            expect(eq.max_durability == 1561,
                   "builtin_eq: diamond_sword max_durability is 1561");
        }
        if (eq.id == "netherite_helmet") {
            found_netherite_helmet = true;
        }
    }

    expect(found_sword, "builtin_eq: diamond_sword found");
    expect(found_netherite_helmet, "builtin_eq: netherite_helmet found");

    std::cout << "  [OK] test_builtin_equipment_lookup" << std::endl;
}

// ---------------------------------------------------------------------------
// Output formatting with empty solutions (no algorithm)
// ---------------------------------------------------------------------------
void test_output_formatting_empty() {
    TagResolver resolver;
    auto ench_infos = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);

    std::vector<EnchSolution> empty_solutions;

    auto verbose = OutputFormatter::format_verbose(empty_solutions, "direct");
    expect(verbose.empty(), "format_verbose: empty solutions produce empty output");

    auto compact = OutputFormatter::format_compact(empty_solutions, "direct");
    expect(compact.find("MODE=direct") != std::string::npos,
           "format_compact: should contain MODE=direct");

    auto json = OutputFormatter::format_json(empty_solutions, "direct");
    expect(json.find("\"solutions\"") != std::string::npos,
           "format_json: should contain solutions array");

    std::cout << "  [OK] test_output_formatting_empty" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== Integration Tests ===" << std::endl;

    try {
        test_full_pipeline_direct();
        test_full_pipeline_inventory();
        test_platform_switching();
        test_builtin_enchantment_lookup();
        test_builtin_equipment_lookup();
        test_output_formatting_empty();

        std::cout << "PASS" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
}
