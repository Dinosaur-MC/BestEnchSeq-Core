// =============================================================================
// BesqContext Acceptance Tests
//
// Tests the public C++ API: profile lifecycle, registry editing, solve pipeline.
// =============================================================================

#include "besq/besq.h"
#include "api/SolvePipeline.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "cli/cli.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "framework/test_utils.h"

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
    input.algorithm = "greedy";
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

    TEST_PASS("BesqContext solve + format");
}

// ---------------------------------------------------------------------------
// Test: Registry editing
// ---------------------------------------------------------------------------

void test_besq_registry_edit() {
    BesqContext ctx;
    ctx.load_builtin();

    // Verify known enchantments exist
    // (get_id returns the index, which is >= 0 for existing enchantments)
    auto sharp_id = ctx.enchantments().get_id("minecraft:sharpness");
    expect(sharp_id >= 0, "sharpness should exist in builtin data");
    (void)sharp_id;

    auto sword_id = ctx.equipment().get_id("minecraft:diamond_sword");
    expect(sword_id >= 0, "diamond_sword should exist");

    TEST_PASS("BesqContext registry access");
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
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        test_context_lifecycle();
        test_besq_solve();
        test_besq_registry_edit();
        test_besq_export();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }

    return print_summary();
}
