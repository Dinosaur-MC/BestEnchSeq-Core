#include "cli/cli.h"
#include "parsers/EnchInfoParser.h"
#include "parsers/EquipmentParser.h"
#include "parsers/InputParser.h"
#include "adapters/OutputFormatter.h"
#include "adapters/RegistryResolver.h"
#include "registries/TagResolver.hpp"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "config/ForgeConfig.h"
#include "framework/test_utils.h"

static auto& test_ench_reg = registries::enchants();
static auto& test_cat_reg  = registries::categories();

#include "adapters/CompactAdapter.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#include "algorithm/IAlgorithm.h"
#include "io/json.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace {

// ---------------------------------------------------------------------------
// Shared helper: validate JSON output structure
// ---------------------------------------------------------------------------
void check_json_solutions(const std::string &json_str, size_t expected_count) {
    auto json = Json::parse(json_str);
    expect(json.type() == JsonType::Object, "json output root must be an object");

    auto root_val = json.get_value();
    auto *root_obj = std::get_if<Json::Object>(&root_val);
    expect(root_obj != nullptr, "json root should be a JSON object");
    if (!root_obj) return;

    // Check required top-level keys
    expect(root_obj->find("solutions") != root_obj->end(),
           "json output must have 'solutions' key");
    expect(root_obj->find("schema_version") != root_obj->end(),
           "json output must have 'schema_version' key");
    expect(root_obj->find("mode") != root_obj->end(),
           "json output must have 'mode' key");

    // Validate solutions array
    auto sol_it = root_obj->find("solutions");
    if (sol_it == root_obj->end()) return;

    auto sol_val = sol_it->second.get_value();
    auto *sol_arr = std::get_if<Json::Array>(&sol_val);
    expect(sol_arr != nullptr, "json 'solutions' must be an array");
    if (!sol_arr) return;

    expect(sol_arr->size() == expected_count,
           "json solutions array size should match expected count");
}

// ---------------------------------------------------------------------------
// Full pipeline: direct mode with builtin data
// ---------------------------------------------------------------------------
void test_full_pipeline_direct() {
    TagResolver resolver;
    registries::categories().initialize();

    auto raw_ench = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, test_cat_reg);
    registries::enchants().initialize(ench_infos);

    const char *argv[] = {"besq", "--target", "diamond_sword", "--wanted", "sharpness=5,knockback=2"};
    
    auto config = parse_cli(5, const_cast<char **>(argv));

    auto raw_eq = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, test_cat_reg);
    std::unordered_map<std::string, const Equipment *> eq_map;
    for (auto &eq : equipments) eq_map[eq.name_id] = &eq;

    // Build ench_id_map from the populated registry
    std::unordered_map<std::string, int32_t> ench_id_map;
    for (const auto& info : test_ench_reg.get_instances())
        ench_id_map[info.name_id] = test_ench_reg.get_id(info.name_id);

    auto input = InputParser::assemble_input(config, ench_id_map, eq_map);

    // sharpness=5 generates 5 books (levels 1..5), knockback=2 generates 2 (levels 1..2)
    expect(input.available_items.size() == 7,
           "full_pipeline_direct: auto-complete should generate 7 graduated books");
    expect(input.target_item.equipment.has_value(),
           "full_pipeline_direct: target should have equipment");
    expect(input.target_item.equipment->name_id == "diamond_sword",
           "full_pipeline_direct: target should be diamond sword");

    std::cout << "  PASS: test_full_pipeline_direct" << std::endl;
}

// ---------------------------------------------------------------------------
// Inventory mode pipeline
// ---------------------------------------------------------------------------
void test_full_pipeline_inventory() {
    TagResolver resolver;
    registries::categories().initialize();
    auto raw_ench = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, test_cat_reg);
    registries::enchants().initialize(ench_infos);

    auto raw_eq = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, test_cat_reg);
    std::unordered_map<std::string, const Equipment *> eq_map;
    for (auto &eq : equipments) eq_map[eq.name_id] = &eq;

    // Write a temp inventory file
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_inv_pipeline";
    std::filesystem::create_directories(temp_dir);
    auto inv_path = (temp_dir / "test_inv_pipeline.json").string();
    {
        std::ofstream f(inv_path);
        f << R"({
            "items": [
                {"type": "book", "enchants": [{"id": "sharpness", "level": 5}], "prior_penalty": 0},
                {"type": "book", "enchants": [{"id": "knockback", "level": 2}], "prior_penalty": 0},
                {"type": "equipment", "id": "diamond_sword", "enchants": [], "prior_penalty": 0, "durability": 1561}
            ]
        })";
    }

    const char *argv[] = {"besq", "--mode", "inventory", "--input", inv_path.c_str(),
                          "--target", "diamond_sword", "--wanted", "sharpness=5"};
    
    auto config = parse_cli(9, const_cast<char **>(argv));

    std::unordered_map<std::string, int32_t> ench_id_map;
    for (const auto& info : test_ench_reg.get_instances())
        ench_id_map[info.name_id] = test_ench_reg.get_id(info.name_id);
    auto input = InputParser::assemble_input(config, ench_id_map, eq_map);

    expect(input.available_items.size() >= 2,
           "full_pipeline_inventory: should have at least 2 items");
    expect(input.target_item.equipment.has_value(),
           "full_pipeline_inventory: target should have equipment");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_full_pipeline_inventory" << std::endl;
}


// ---------------------------------------------------------------------------
// Enchantment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_enchantment_lookup() {
    TagResolver resolver;
    registries::categories().initialize();
    auto raw_ench = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, test_cat_reg);
    registries::enchants().initialize(ench_infos);

    expect(registries::enchants().get_id("sharpness") >= 0, "builtin: sharpness found");
    expect(registries::enchants().get_id("nonexistent") < 0, "builtin: nonexistent not found");

    auto &sharpness = registries::enchants().get("sharpness");
    expect(sharpness.name_id == "sharpness",
           "builtin: sharpness name_id is sharpness");
    expect(sharpness.max_level == 5, "builtin: sharpness max_level is 5");
    expect(sharpness.multiplier == 1, "builtin: sharpness multiplier is 1");

    std::cout << "  PASS: test_builtin_enchantment_lookup" << std::endl;
}

// ---------------------------------------------------------------------------
// Equipment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_equipment_lookup() {
    TagResolver resolver;
    registries::categories().initialize();
    auto raw_eq = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, test_cat_reg);

    bool found_sword = false;
    bool found_netherite_helmet = false;
    for (const auto &eq : equipments) {
        if (eq.name_id == "diamond_sword") {
            found_sword = true;
            expect(eq.category_id == EquipmentCategory::ID_SWORD,
                   "builtin_eq: diamond_sword category is sword");
            expect(eq.max_durability == 1561,
                   "builtin_eq: diamond_sword max_durability is 1561");
        }
        if (eq.name_id == "netherite_helmet") {
            found_netherite_helmet = true;
        }
    }

    expect(found_sword, "builtin_eq: diamond_sword found");
    expect(found_netherite_helmet, "builtin_eq: netherite_helmet found");

    std::cout << "  PASS: test_builtin_equipment_lookup" << std::endl;
}

// ---------------------------------------------------------------------------
// Output formatting with empty solutions (no algorithm)
// ---------------------------------------------------------------------------
void test_output_formatting_empty() {
    TagResolver resolver;
    registries::categories().initialize();
    auto raw_ench = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, test_cat_reg);
    registries::enchants().initialize(ench_infos);

    std::vector<EnchSolution> empty_solutions;

    auto verbose = OutputFormatter::format_verbose(empty_solutions, test_ench_reg, test_cat_reg, "direct");
    expect(verbose.empty(), "format_verbose: empty solutions produce empty output");

    auto compact = OutputFormatter::format_compact(empty_solutions, test_ench_reg, test_cat_reg, "direct");
    expect(compact.find("MODE=direct") != std::string::npos,
           "format_compact: should contain MODE=direct");

    auto json_str = OutputFormatter::format_json(empty_solutions, test_ench_reg, test_cat_reg, "direct");
    expect(json_str.find("\"solutions\"") != std::string::npos,
           "format_json: should contain solutions array");
    // Structured JSON validation
    check_json_solutions(json_str, 0);

    std::cout << "  PASS: test_output_formatting_empty" << std::endl;
}

// ---------------------------------------------------------------------------
// End-to-end: full pipeline (parse -> execute -> format)
// ---------------------------------------------------------------------------
void test_full_pipeline_execute() {
    TagResolver resolver;
    registries::categories().initialize();
    auto raw_ench = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, test_cat_reg);
    registries::enchants().initialize(ench_infos);

    auto raw_eq = EquipmentParser::parse_native_json(
        "data/builtin/vanilla.json", resolver);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, test_cat_reg);
    std::unordered_map<std::string, const Equipment *> eq_map;
    for (auto &eq : equipments) eq_map[eq.name_id] = &eq;

    // 1. Parse CLI for a simple case
    const char *argv[] = {"besq", "--target", "diamond_sword", "--wanted", "sharpness=3"};
    
    auto config = parse_cli(5, const_cast<char **>(argv));

    // 2. Build domain input
    auto equip_it = eq_map.find("diamond_sword");
    expect(equip_it != eq_map.end(),
           "execute: diamond_sword found in equipment map");

    auto wanted_specs = parse_enchantment_list(config.wanted);
    std::unordered_map<std::string, int32_t> ench_id_map;
    for (const auto& info : test_ench_reg.get_instances())
        ench_id_map[info.name_id] = test_ench_reg.get_id(info.name_id);
    EnchSet wanted = InputParser::build_wanted_enchset(wanted_specs, ench_id_map);
    EnchSet existing;    // equipment starts empty
    ItemCollection books = InputParser::generate_books(wanted, existing);
    expect(books.size() == 3,
           "execute: 3 graduated books for sharpness=3 (levels 1,2,3)");

    // 3. Build AlgorithmInput via CompactAdapter
    ItemStack target_item(*equip_it->second, wanted, 0);

    ForgeConfig forge_config;
    forge_config.platform = MCE::Java;

    CompactAdapter adapter;
    AlgorithmInput algo_input = adapter.apply(
        target_item, existing, books, forge_config, registries::enchants());

    expect(algo_input.target.size() == 1,
           "execute: target should have 1 enchantment (sharpness 3)");
    expect(algo_input.items.size() == 1 + books.size(),
           "execute: items = 1 equipment + N books");

    // 4. Create algorithm (Greedy for speed) and executor
    auto algo = std::make_unique<GreedyAlgorithm>(forge_config);
    AlgorithmExecutor executor(std::move(algo));
    executor.start(algo_input);

    // 5. Wait for completion
    auto state = executor.wait();
    expect(state == AlgorithmState::Completed,
           "execute: algorithm should complete successfully");
    expect(executor.state() == AlgorithmState::Completed,
           "execute: state should be Completed after wait");

    // 6. Check output
    auto output = executor.output();
    expect(output.is_valid,
           "execute: output should be valid");
    expect(!output.solutions.empty(),
           "execute: should have at least one solution");

    // 7. Convert back to domain solutions
    auto solutions = adapter.recall(output, algo_input,
                                     existing, target_item, books);
    expect(!solutions.empty(),
           "execute: should have at least one domain solution");
    expect(solutions[0].is_success,
           "execute: solution should be a success");
    expect(!solutions[0].steps.empty(),
           "execute: solution should have at least one forge step");

    // 8. Format output in all 3 formats and verify content
    //    Verbose
    auto verbose_text = OutputFormatter::format_verbose(solutions, test_ench_reg, test_cat_reg, "direct");
    expect(!verbose_text.empty(),
           "execute: verbose output should not be empty");
    expect(verbose_text.find("sharpness") != std::string::npos,
           "execute: verbose output should contain 'sharpness'");

    //    Compact
    auto compact_text = OutputFormatter::format_compact(solutions, test_ench_reg, test_cat_reg, "direct");
    expect(!compact_text.empty(),
           "execute: compact output should not be empty");
    expect(compact_text.find("MODE=direct") != std::string::npos,
           "execute: compact output should contain MODE=direct");
    expect(compact_text.find("sharpness") != std::string::npos,
           "execute: compact output should contain 'sharpness'");

    //    JSON
    auto json_text = OutputFormatter::format_json(solutions, test_ench_reg, test_cat_reg, "direct");
    expect(!json_text.empty(),
           "execute: JSON output should not be empty");
    expect(json_text.find("\"is_success\"") != std::string::npos,
           "execute: JSON output should contain is_success");
    expect(json_text.find("sharpness") != std::string::npos,
           "execute: JSON output should contain 'sharpness'");
    check_json_solutions(json_text, 1);

    std::cout << "  PASS: test_full_pipeline_execute" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== Integration Tests ===" << std::endl;

    try {
        test_full_pipeline_direct();
        test_full_pipeline_inventory();
        test_builtin_enchantment_lookup();
        test_builtin_equipment_lookup();
        test_output_formatting_empty();
        test_full_pipeline_execute();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
