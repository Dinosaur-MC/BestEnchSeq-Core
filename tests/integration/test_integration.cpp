// =============================================================================
// Integration Tests
//
// Tests the full pipeline with builtin data across all formatting modes.
// =============================================================================

#include "builtin/DataLoader.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Profile.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/orchestration/pipelines/SolvePipeline.h"
#define BESQ_TEST_MAIN

#include "framework/test_framework.h"

#include <iostream>

namespace {

// ---------------------------------------------------------------------------
// Helper: build a test Profile loaded with builtin vanilla data
// ---------------------------------------------------------------------------
Profile make_builtin_profile() {
    TagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    besq::data::load_builtin_data(cat_reg, ench_reg, eq_reg);

    ProfileMetadata meta("test:integration");
    Profile profile(std::move(meta), std::move(ench_reg), std::move(eq_reg), std::move(cat_reg));
    // Attach the real vanilla tag-universe resolver (T10: real MC item tags
    // are the applicability source of truth, not the category-derived fallback).
    profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());
    return profile;
}

// ---------------------------------------------------------------------------
// Helper: build a SolveRequest for simple direct-mode tests
// ---------------------------------------------------------------------------
SolveRequest make_direct_request(const Item& target_item, const std::string& algo = "hamming") {
    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{};
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;
    request.algorithm = algo;
    return request;
}

// ---------------------------------------------------------------------------
// Full pipeline: direct mode with builtin data
// ---------------------------------------------------------------------------
void test_full_pipeline_direct() {
    auto profile = make_builtin_profile();

    // Parse target using inline syntax
    Item target_item = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]", profile.ench(), profile.eq());

    // Build and run the solve pipeline
    SolveRequest request = make_direct_request(target_item);
    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    auto result = SolvePipeline::run(profile, request, loader);

    expect(result.success, "full_pipeline_direct: solve should succeed");
    expect(!result.solutions.empty(), "full_pipeline_direct: should have solutions");
    expect(result.solutions[0].is_success, "full_pipeline_direct: first solution should succeed");
    expect(result.solutions[0].total_exp_level_cost > 0, "full_pipeline_direct: cost should be positive");
    expect(!result.solutions[0].steps.empty(), "full_pipeline_direct: should have forge steps");

    TEST_PASS("test_full_pipeline_direct");
}

// ---------------------------------------------------------------------------
// Inventory mode pipeline
// ---------------------------------------------------------------------------
void test_full_pipeline_inventory() {
    auto profile = make_builtin_profile();

    // Build target with inline syntax
    Item target_item = ItemParser::parse("diamond_sword[sharpness=5]", profile.ench(), profile.eq());

    // Build available items to simulate inventory
    ItemCollection available_items;
    {
        EnchSet book_enchs;
        book_enchs.emplace(NSID("minecraft:sharpness"), "sharpness", 5);
        available_items.emplace_back(NSID("minecraft:enchanted_book"), book_enchs, 0);
    }
    {
        EnchSet book_enchs;
        book_enchs.emplace(NSID("minecraft:knockback"), "knockback", 2);
        available_items.emplace_back(NSID("minecraft:enchanted_book"), book_enchs, 0);
    }
    {
        available_items.emplace_back(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    }

    expect(available_items.size() >= 2, "full_pipeline_inventory: should have at least 2 items");
    expect(!target_item.id.empty(), "full_pipeline_inventory: target should have equipment");

    // Run the inventory solve end-to-end: the clean diamond_sword in the
    // payload becomes items[0]; the books form the sacrifice pool.
    std::vector<int32_t> priorities = {1, 2, 10};
    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    request.payload = InventoryPayload{available_items, priorities};
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;
    request.algorithm = "hamming";

    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    auto result = SolvePipeline::run(profile, request, loader);

    expect(result.success, "full_pipeline_inventory: solve should succeed");
    expect(!result.solutions.empty(), "full_pipeline_inventory: should have solutions");
    expect(result.solutions[0].is_success, "full_pipeline_inventory: first solution should succeed");
    expect(!result.solutions[0].steps.empty(), "full_pipeline_inventory: should have forge steps");

    TEST_PASS("test_full_pipeline_inventory");
}

// ---------------------------------------------------------------------------
// Enchantment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_enchantment_lookup() {
    auto profile = make_builtin_profile();

    expect(profile.ench().contains(NSID("minecraft:sharpness")), "builtin: sharpness found");
    expect(!profile.ench().contains(NSID("minecraft:nonexistent")), "builtin: nonexistent not found");

    auto& sharpness = profile.ench().at(NSID("minecraft:sharpness"));
    expect(sharpness.id.str() == "minecraft:sharpness", "builtin: sharpness id is minecraft:sharpness");
    expect(sharpness.max_level == 5, "builtin: sharpness max_level is 5");
    expect(sharpness.multiplier == 1, "builtin: sharpness multiplier is 1");

    TEST_PASS("test_builtin_enchantment_lookup");
}

// ---------------------------------------------------------------------------
// Equipment lookup from builtin data
// ---------------------------------------------------------------------------
void test_builtin_equipment_lookup() {
    auto profile = make_builtin_profile();

    auto& equipments = profile.eq().data();

    bool found_sword = false;
    bool found_netherite_helmet = false;
    for (const auto& [nsid, eq] : equipments) {
        if (eq.id.str() == "minecraft:diamond_sword") {
            found_sword = true;
            expect(eq.category == EquipmentTag::sword(), "builtin_eq: diamond_sword category is sword");
            expect(eq.max_durability == 1561, "builtin_eq: diamond_sword max_durability is 1561");
        }
        if (eq.id.str() == "minecraft:netherite_helmet") {
            found_netherite_helmet = true;
        }
    }

    expect(found_sword, "builtin_eq: diamond_sword found");
    expect(found_netherite_helmet, "builtin_eq: netherite_helmet found");

    TEST_PASS("test_builtin_equipment_lookup");
}

// ---------------------------------------------------------------------------
// Output formatting with empty solutions (no algorithm)
// ---------------------------------------------------------------------------
void test_output_formatting_empty() {
    auto profile = make_builtin_profile();

    std::vector<Solution> empty_solutions;

    auto verbose = OutputFormatter::format_verbose(empty_solutions, profile, AlgorithmMode::direct);
    expect(verbose.empty(), "format_verbose: empty solutions produce empty output");

    auto compact = OutputFormatter::format_compact(empty_solutions, profile, AlgorithmMode::direct);
    expect(compact.find("MODE=direct") != std::string::npos, "format_compact: should contain MODE=direct");

    auto json_str = OutputFormatter::format_json(empty_solutions, profile, AlgorithmMode::direct);
    expect(json_str.find("\"solutions\"") != std::string::npos, "format_json: should contain solutions array");

    TEST_PASS("test_output_formatting_empty");
}

// ---------------------------------------------------------------------------
// End-to-end: full pipeline (parse -> execute -> format)
// ---------------------------------------------------------------------------
void test_full_pipeline_execute() {
    auto profile = make_builtin_profile();

    // 1. Parse CLI using inline target syntax
    Item target_item = ItemParser::parse("diamond_sword[sharpness=3]", profile.ench(), profile.eq());
    expect(!target_item.id.empty(), "execute: target should have equipment");

    // 2. Build solve request and run pipeline
    SolveRequest request = make_direct_request(target_item, "hamming");

    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    auto result = SolvePipeline::run(profile, request, loader);

    expect(result.success, "execute: solve should succeed");
    expect(!result.solutions.empty(), "execute: should have solutions");
    expect(result.solutions[0].is_success, "execute: first solution should succeed");
    expect(!result.solutions[0].steps.empty(), "execute: solution should have at least one forge step");

    // 3. Format output in all 3 formats and verify content
    auto verbose_text = OutputFormatter::format_verbose(result.solutions, profile, AlgorithmMode::direct);
    expect(!verbose_text.empty(), "execute: verbose output should not be empty");
    expect(verbose_text.find("sharpness") != std::string::npos, "execute: verbose output should contain 'sharpness'");

    auto compact_text = OutputFormatter::format_compact(result.solutions, profile, AlgorithmMode::direct);
    expect(!compact_text.empty(), "execute: compact output should not be empty");
    expect(compact_text.find("MODE=direct") != std::string::npos, "execute: compact output should contain MODE=direct");
    expect(compact_text.find("sharpness") != std::string::npos, "execute: compact output should contain 'sharpness'");

    auto json_text = OutputFormatter::format_json(result.solutions, profile, AlgorithmMode::direct);
    expect(!json_text.empty(), "execute: JSON output should not be empty");
    expect(json_text.find("\"is_success\"") != std::string::npos, "execute: JSON output should contain is_success");
    expect(json_text.find("sharpness") != std::string::npos, "execute: JSON output should contain 'sharpness'");

    TEST_PASS("test_full_pipeline_execute");
}

} // anonymous namespace

TEST_CASE("test_integration") {
    test_full_pipeline_direct();
    test_full_pipeline_inventory();
    test_builtin_enchantment_lookup();
    test_builtin_equipment_lookup();
    test_output_formatting_empty();
    test_full_pipeline_execute();
}
