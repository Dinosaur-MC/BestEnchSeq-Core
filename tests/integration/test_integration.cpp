#include "domain/interface/cli/cli.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/interface/parsers/EnchParser.h"
#include "domain/interface/parsers/ItemParser.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
// REMOVED: RegistryAccess.h — create local registries instead
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "framework/test_utils.h"

static EnchantmentRegistry test_ench_reg;
static EquipmentTagRegistry test_cat_reg;

#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/_strategies/hamming/HammingAlgorithm.h"
#include "domain/algorithm/IAlgorithm.h"
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
    test_cat_reg.initialize();

    auto [raw_ench, raw_eq] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);

    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, test_cat_reg);
    EquipmentRegistry eq_reg;
    eq_reg.initialize(equipments);

    // Use inline target syntax: diamond_sword[sharpness=5,knockback=2]
    const char *argv[] = {"besq", "--target", "diamond_sword[sharpness=5,knockback=2]"};
    auto config = parse_cli(3, const_cast<char **>(argv));

    // Build domain input from CLI spec
    auto target_spec = ItemParser::parse(config.target);
    Item target_item = build_target(target_spec, test_ench_reg, eq_reg);

    EnchSet source_ench;  // no --source flag
    EnchSet target_ench = build_enchset(target_spec.inline_enchants, test_ench_reg);

    auto resolved = ItemResolver::resolve(target_item, source_ench, target_ench, test_ench_reg);

    // sharpness=5 generates 5 books (levels 1..5), knockback=2 generates 2 (levels 1..2)
    expect(resolved.available_items.size() == 7,
           "full_pipeline_direct: auto-complete should generate 7 graduated books");
    expect(!resolved.target_item.id.empty(),
           "full_pipeline_direct: target should have equipment");
    expect(resolved.target_item.id.str() == "minecraft:diamond_sword",
           "full_pipeline_direct: target should be diamond sword");

    std::cout << "  PASS: test_full_pipeline_direct" << std::endl;
}

// ---------------------------------------------------------------------------
// Inventory mode pipeline
// ---------------------------------------------------------------------------
void test_full_pipeline_inventory() {
    test_cat_reg.initialize();
    auto [raw_ench, raw_eq] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);

    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, test_cat_reg);
    EquipmentRegistry eq_reg;
    eq_reg.initialize(equipments);

    // Build target with inline syntax
    TargetSpec target_spec;
    target_spec.item_id = "diamond_sword";
    target_spec.inline_enchants.push_back({"minecraft", "sharpness", 5});
    Item target_item = build_target(target_spec, test_ench_reg, eq_reg);

    // Build available items to simulate inventory
    ItemCollection available_items;
    {
        EnchSet book_enchs;
        book_enchs.emplace(NSID("sharpness"), "sharpness", 5);
        available_items.emplace_back(NSID("minecraft:enchanted_book"), book_enchs, 0);
    }
    {
        EnchSet book_enchs;
        book_enchs.emplace(NSID("knockback"), "knockback", 2);
        available_items.emplace_back(NSID("minecraft:enchanted_book"), book_enchs, 0);
    }
    {
        available_items.emplace_back(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    }

    expect(available_items.size() >= 2,
           "full_pipeline_inventory: should have at least 2 items");
    expect(!target_item.id.empty(),
           "full_pipeline_inventory: target should have equipment");

    std::cout << "  PASS: test_full_pipeline_inventory" << std::endl;
}


// ---------------------------------------------------------------------------
// Enchantment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_enchantment_lookup() {
    test_cat_reg.initialize();
    auto [raw_ench, _] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);

    expect(test_ench_reg.get_id(NSID("minecraft:sharpness")) >= 0, "builtin: sharpness found");
    expect(test_ench_reg.get_id(NSID("nonexistent")) < 0, "builtin: nonexistent not found");

    auto &sharpness = test_ench_reg.get(NSID("minecraft:sharpness"));
    expect(sharpness.id.str() == "minecraft:sharpness",
           "builtin: sharpness id is minecraft:sharpness");
    expect(sharpness.max_level == 5, "builtin: sharpness max_level is 5");
    expect(sharpness.multiplier == 1, "builtin: sharpness multiplier is 1");

    std::cout << "  PASS: test_builtin_enchantment_lookup" << std::endl;
}

// ---------------------------------------------------------------------------
// Equipment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_equipment_lookup() {
    test_cat_reg.initialize();
    auto [_, raw_eq] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, test_cat_reg);

    bool found_sword = false;
    bool found_netherite_helmet = false;
    for (const auto &eq : equipments) {
        if (eq.id.str() == "minecraft:diamond_sword") {
            found_sword = true;
            expect(eq.category == EquipmentTag::sword(),
                   "builtin_eq: diamond_sword category is sword");
            expect(eq.max_durability == 1561,
                   "builtin_eq: diamond_sword max_durability is 1561");
        }
        if (eq.id.str() == "minecraft:netherite_helmet") {
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
    test_cat_reg.initialize();
    auto [raw_ench, _] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);

    std::vector<Solution> empty_solutions;

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
    test_cat_reg.initialize();
    auto [raw_ench, raw_eq] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);

    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, test_cat_reg);
    EquipmentRegistry eq_reg;
    eq_reg.initialize(equipments);

    // 1. Parse CLI using inline target syntax
    const char *argv[] = {"besq", "--target", "diamond_sword[sharpness=3]"};
    auto config = parse_cli(3, const_cast<char **>(argv));

    // 2. Build domain input
    auto target_spec = ItemParser::parse(config.target);
    Item target_item = build_target(target_spec, test_ench_reg, eq_reg);
    expect(!target_item.id.empty(),
           "execute: target should have equipment");

    EnchSet existing;    // equipment starts empty
    EnchSet target_ench = build_enchset(target_spec.inline_enchants, test_ench_reg);

    // Use ItemResolver to validate and generate graduated books
    auto resolved = ItemResolver::resolve(target_item, existing, target_ench, test_ench_reg);
    expect(resolved.available_items.size() == 3,
           "execute: 3 graduated books for sharpness=3 (levels 1,2,3)");

    // 3. Build AlgorithmInput via CompactAdapter (new API)
    AlgorithmInput algo_input = CompactAdapter::apply(resolved, test_ench_reg);
    algo_input.config.platform = MCE::Java;

    expect(algo_input.target.size() == 1,
           "execute: target should have 1 enchantment (sharpness 3)");
    expect(algo_input.items.size() == 1 + resolved.available_items.size(),
           "execute: items = 1 equipment + N books");

    // 4. Create algorithm (Hamming for speed) and executor
    auto algo = std::make_unique<HammingAlgorithm>();
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
    auto solutions = CompactAdapter::recall(output, algo_input,
                                     resolved.source_ench, resolved.target_item, resolved.available_items);
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
