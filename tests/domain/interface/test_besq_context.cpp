// =============================================================================
// BesqContext Acceptance Tests
//
// Tests the public C++ API: profile lifecycle, registry editing, solve pipeline.
// =============================================================================

#include "besq/besq.h"
#include "besq/besq_abi.h"
#include "domain/interface/SolvePipeline.h"
#include "domain/interface/parsers/EnchParser.h"
#include "domain/interface/parsers/ItemParser.h"
#include "domain/interface/cli/cli.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "framework/test_utils.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Test: Context lifecycle
// ---------------------------------------------------------------------------

void test_context_lifecycle() {
    BesqContext ctx;
    ctx.load_builtin();

    expect(ctx.active_profile() == "default", "default profile after load_builtin");
    expect(ctx.list_profiles().size() == 1, "one profile after load_builtin");

    // Fork and switch
    ctx.fork_profile("default", "testing");
    expect(ctx.list_profiles().size() == 2, "two profiles after fork");

    ctx.activate_profile("testing");
    expect(ctx.active_profile() == "testing", "switched to testing");

    // Switch back
    ctx.activate_profile("default");
    expect(ctx.active_profile() == "default", "back to default");

    // Remove
    ctx.remove_profile("testing");
    expect(ctx.list_profiles().size() == 1, "one profile after remove");

    TEST_PASS("BesqContext lifecycle");
}

// ---------------------------------------------------------------------------
// Test: Fork / merge correctness
// ---------------------------------------------------------------------------

void test_fork_merge() {
    BesqContext ctx;
    ctx.load_builtin();

    // Fork default -> modded
    ctx.fork_profile("default", "modded");
    ctx.activate_profile("modded");

    // Add custom enchantment to modded profile
    EnchInfo custom;
    custom.name_id = "custom:test_ench";
    custom.max_level = 3;
    custom.multiplier = 2;
    custom.applicable_category_ids.insert(0); // "any" category
    bool added = ctx.add_enchantment(custom);
    expect(added, "add custom enchantment to modded profile");

    int32_t custom_id = ctx.enchantments().get_id("custom:test_ench");
    expect(custom_id >= 0, "custom ench exists in modded after add");

    // Default profile should NOT have custom
    ctx.activate_profile("default");
    int32_t default_id = ctx.enchantments().get_id("custom:test_ench");
    expect(default_id < 0, "custom ench NOT in default profile");

    // Merge modded -> default
    ctx.merge_profile("modded", "default");
    default_id = ctx.enchantments().get_id("custom:test_ench");
    expect(default_id >= 0, "custom ench merged to default");

    // Cleanup
    ctx.remove_profile("modded");

    TEST_PASS("BesqContext fork/merge correctness");
}

// ---------------------------------------------------------------------------
// Test: Solve via BesqContext
// ---------------------------------------------------------------------------

void test_besq_solve() {
    BesqContext ctx;
    ctx.load_builtin();

    // Build SolveInput using ItemParser + CLI helpers (same as main.cpp)
    std::string target_str = "diamond_sword[sharpness=3]";
    const char* argv[] = {"besq", "--target", target_str.c_str()};
    auto config = parse_cli(3, const_cast<char**>(argv));

    auto target_spec = ItemParser::parse(config.target);
    auto target_item = build_target(target_spec, ctx.enchantments(), ctx.equipment());

    SolveInput input;
    input.target_item = target_item;
    input.algorithm = "hamming";
    input.forge_config.platform = MCE::Java;
    input.search_config.max_solutions = 1;

    auto result = ctx.solve(input);
    expect(result.success, "solve should succeed");
    expect(!result.solutions.empty(), "should have solutions");
    expect(result.solutions[0].is_success, "first solution should succeed");
    expect(result.solutions[0].total_exp_level_cost > 0, "cost should be positive");
    expect(!result.solutions[0].steps.empty(), "should have forge steps");

    // Verify formatting works
    auto json_out = result.to_json(ctx.enchantments(), ctx.categories());
    expect(json_out.find("sharpness") != std::string::npos,
           "JSON output should mention sharpness");

    auto text_out = result.to_text(ctx.enchantments(), ctx.categories());
    expect(!text_out.empty(), "text output should be non-empty");

    // Verify raw JSON output works (no registry deps)
    auto raw_json = result.to_json_raw();
    expect(raw_json.find("algorithm") != std::string::npos,
           "raw JSON should contain algorithm field");
    expect(raw_json.find("solutions") != std::string::npos,
           "raw JSON should contain solutions field");

    TEST_PASS("BesqContext solve + format");
}

// ---------------------------------------------------------------------------
// Test: Registry editing (add / modify / remove)
// ---------------------------------------------------------------------------

void test_besq_registry_edit() {
    BesqContext ctx;
    ctx.load_builtin();

    // Verify known enchantments exist
    auto sharp_id = ctx.enchantments().get_id("minecraft:sharpness");
    expect(sharp_id >= 0, "sharpness should exist in builtin data");
    (void)sharp_id;

    auto sword_id = ctx.equipment().get_id("minecraft:diamond_sword");
    expect(sword_id >= 0, "diamond_sword should exist");

    // ── Add custom enchantment ──
    EnchInfo custom;
    custom.name_id = "custom:test_ench";
    custom.max_level = 3;
    custom.multiplier = 2;
    custom.applicable_category_ids.insert(0); // "any" category
    bool added = ctx.add_enchantment(custom);
    expect(added, "add custom enchantment");

    int32_t custom_id = ctx.enchantments().get_id("custom:test_ench");
    expect(custom_id >= 0, "custom enchantment findable after add");

    // ── Modify enchantment ──
    EnchInfo patch;
    patch.max_level = 5;
    bool modded = ctx.modify_enchantment("custom:test_ench", patch);
    expect(modded, "modify custom enchantment");
    // Verify via get_id still works (modify doesn't change name_id)
    custom_id = ctx.enchantments().get_id("custom:test_ench");
    expect(custom_id >= 0, "custom enchantment still findable after modify");

    // ── Remove enchantment ──
    bool removed = ctx.remove_enchantment("custom:test_ench");
    expect(removed, "remove custom enchantment");
    custom_id = ctx.enchantments().get_id("custom:test_ench");
    expect(custom_id < 0, "custom enchantment gone after remove");

    // ── Duplicate add should fail ──
    added = ctx.add_enchantment(custom);
    expect(added, "re-add custom enchantment after removal");

    bool dup = ctx.add_enchantment(custom);
    expect(!dup, "duplicate add should return false");

    // ── Remove non-existent should fail ──
    removed = ctx.remove_enchantment("nonexistent:fake");
    expect(!removed, "remove non-existent should return false");

    TEST_PASS("BesqContext registry editing");
}

// ---------------------------------------------------------------------------
// Test: Export registry
// ---------------------------------------------------------------------------

void test_besq_export() {
    BesqContext ctx;
    ctx.load_builtin();

    const std::string test_path = "besq_ctx_test_export.json";
    bool ok = ctx.export_registry(test_path);
    expect(ok, "export should succeed");

    if (std::filesystem::exists(test_path)) {
        expect(std::filesystem::file_size(test_path) > 0, "export file should not be empty");
        std::filesystem::remove(test_path);
    }

    TEST_PASS("BesqContext export");
}

// ---------------------------------------------------------------------------
// Test: C ABI bindings
// ---------------------------------------------------------------------------

void test_c_abi() {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");

    int rc = besq_load_builtin(ctx);
    expect(rc == 0, "c abi load_builtin");

    // Profile management
    const char* active = besq_active_profile(ctx);
    expect(active != nullptr, "c abi active_profile");
    expect(std::string(active) == "default", "c abi default profile");

    int out_count = 0;
    char** profiles = besq_list_profiles(ctx, &out_count);
    expect(profiles != nullptr, "c abi list_profiles should return non-null");
    expect(out_count == 1, "c abi one profile");
    besq_free_string_list(profiles, out_count);

    rc = besq_fork_profile(ctx, "default", "testing");
    expect(rc == 0, "c abi fork_profile");
    rc = besq_activate_profile(ctx, "testing");
    expect(rc == 0, "c abi activate_profile");
    active = besq_active_profile(ctx);
    expect(std::string(active) == "testing", "c abi active profile testing");

    rc = besq_activate_profile(ctx, "default");
    expect(rc == 0, "c abi activate default back");
    rc = besq_remove_profile(ctx, "testing");
    expect(rc == 0, "c abi remove_profile");

    // Solve via C ABI
    const char* json_input =
        "{\"target\":{\"equipment\":\"diamond_sword\",\"enchantments\":[{\"id\":\"sharpness\",\"level\":3}]},"
        "\"algorithm\":\"hamming\",\"platform\":\"java\",\"max_solutions\":1}";
    const char* json_result = besq_solve(ctx, json_input);
    if (json_result) {
        std::string result(json_result);
        expect(result.find("solutions") != std::string::npos,
               "c abi solve returns solutions");
        // Should also contain cost info
        expect(result.find("exp") != std::string::npos || result.find("cost") != std::string::npos,
               "c abi solve result contains cost info");
        besq_free_string(const_cast<char*>(json_result));
    } else {
        // If solve fails, check last_error
        const char* err = besq_last_error(ctx);
        if (err) {
            std::cerr << "C ABI solve error: " << err << std::endl;
        }
        expect(json_result != nullptr, "c abi solve should succeed");
    }

    // Export test
    rc = besq_export_registry(ctx, "besq_abi_test_export.json");
    expect(rc == 0, "c abi export_registry");
    if (std::filesystem::exists("besq_abi_test_export.json")) {
        expect(std::filesystem::file_size("besq_abi_test_export.json") > 0,
               "c abi export file not empty");
        std::filesystem::remove("besq_abi_test_export.json");
    }

    besq_destroy(ctx);

    TEST_PASS("BesqContext C ABI");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        test_context_lifecycle();
        test_fork_merge();
        test_besq_solve();
        test_besq_registry_edit();
        test_besq_export();
        test_c_abi();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }

    return print_summary();
}
