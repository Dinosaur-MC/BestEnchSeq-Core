// =============================================================================
// BesqContext Acceptance Tests
//
// Tests the public C++ API: profile lifecycle, registry editing, solve pipeline.
// =============================================================================

#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/BesqContext.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "besq/besq.h"
#include "framework/test_utils.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

// ---------------------------------------------------------------------------
// Test: Context lifecycle
// ---------------------------------------------------------------------------

void test_context_lifecycle() {
    BesqContext ctx;
    ctx.load_builtin();

    expect(ctx.active_profile() == "builtin:vanilla", "default profile after load_builtin");
    expect(ctx.list_profiles().size() == 1, "one profile after load_builtin");

    // Fork and switch
    ctx.fork_profile("builtin:vanilla", "minecraft:testing");
    expect(ctx.list_profiles().size() == 2, "two profiles after fork");

    ctx.activate_profile("minecraft:testing");
    expect(ctx.active_profile() == "minecraft:testing", "switched to testing");

    // Switch back
    ctx.activate_profile("builtin:vanilla");
    expect(ctx.active_profile() == "builtin:vanilla", "back to default");

    // Remove
    ctx.remove_profile("minecraft:testing");
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
    ctx.fork_profile("builtin:vanilla", "minecraft:modded");
    ctx.activate_profile("minecraft:modded");

    // Add custom enchantment to modded profile
    EnchInfo custom;
    custom.id = NSID("custom:test_ench");
    custom.name = "Test Enchantment";
    custom.max_level = 3;
    custom.multiplier = 2;
    custom.supported_items.insert(NSID("#minecraft:enchantable/equippable"));
    bool added = ctx.add_enchantment(custom);
    expect(added, "add custom enchantment to modded profile");

    expect(ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom ench exists in modded after add");

    // Default profile should NOT have custom
    ctx.activate_profile("builtin:vanilla");
    expect(!ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom ench NOT in default profile");

    // Merge modded -> default
    ctx.merge_profile("minecraft:modded", "builtin:vanilla");
    expect(ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom ench merged to default");

    // Cleanup
    ctx.remove_profile("minecraft:modded");

    TEST_PASS("BesqContext fork/merge correctness");
}

// ---------------------------------------------------------------------------
// Test: Solve via BesqContext
// ---------------------------------------------------------------------------

void test_besq_solve() {
    BesqContext ctx;
    ctx.load_builtin();

    // Use inline target syntax: diamond_sword[sharpness=3]
    std::string target_str = "diamond_sword[sharpness=3]";
    const char* argv[] = {"besq", "--target", target_str.c_str()};
    auto config = CLIApp::parse(3, const_cast<char**>(argv));

    // Parse target item using the context's registries
    Item target_item = ItemParser::parse(config.target,
                                         ctx.enchantments(), ctx.equipment());

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{};
    request.algorithm = "hamming";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;

    auto result = ctx.solve(request);
    expect(result.success, "solve should succeed");
    expect(!result.solutions.empty(), "should have solutions");
    expect(result.solutions[0].is_success, "first solution should succeed");
    expect(result.solutions[0].total_exp_level_cost > 0, "cost should be positive");
    expect(!result.solutions[0].steps.empty(), "should have forge steps");

    // Verify formatting works via BesqContext
    auto json_out = ctx.format(result, AlgorithmMode::direct, "json");
    expect(json_out.find("sharpness") != std::string::npos,
           "JSON output should mention sharpness");

    auto text_out = ctx.format(result, AlgorithmMode::direct, "text");
    expect(!text_out.empty(), "text output should be non-empty");

    TEST_PASS("BesqContext solve + format");
}

// ---------------------------------------------------------------------------
// Test: Solve when source already satisfies the target (goal already met)
// ---------------------------------------------------------------------------

void test_besq_solve_already_met() {
    BesqContext ctx;
    ctx.load_builtin();

    // Source == target: the current state already meets the goal.  This must
    // produce a 0-step solution, NOT "目标不可达".
    const char* argv[] = {"besq", "--target", "diamond_sword[sharpness=5]",
                          "--source", "sharpness=5"};
    auto config = CLIApp::parse(5, const_cast<char**>(argv));

    Item target_item = ItemParser::parse(config.target,
                                         ctx.enchantments(), ctx.equipment());

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{
        EnchParser::parse(config.source, ctx.enchantments())
    };
    request.algorithm = "dp_merge";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;

    auto result = ctx.solve(request);
    expect(result.success, "already-met solve should succeed (not unreachable)");
    expect(!result.solutions.empty(), "already-met solve should produce a solution");
    expect(result.solutions[0].is_success, "already-met solution should succeed");
    expect(result.solutions[0].steps.empty(), "already-met solve should be 0 steps");
    expect(result.solutions[0].total_exp_level_cost == 0,
           "already-met solve cost should be 0");

    auto text_out = ctx.format(result, AlgorithmMode::direct, "text");
    expect(!text_out.empty(), "already-met text output should be non-empty");

    TEST_PASS("BesqContext solve already-met");
}

// ---------------------------------------------------------------------------
// Test: Registry editing (add / modify / remove)
// ---------------------------------------------------------------------------

void test_besq_registry_edit() {
    BesqContext ctx;
    ctx.load_builtin();

    // Verify known enchantments exist
    expect(ctx.enchantments().contains(NSID("minecraft:sharpness")),
           "sharpness should exist in builtin data");

    expect(ctx.equipment().contains(NSID("minecraft:diamond_sword")),
           "diamond_sword should exist");

    // ── Add custom enchantment ──
    EnchInfo custom;
    custom.id = NSID("custom:test_ench");
    custom.name = "Test Enchantment";
    custom.max_level = 3;
    custom.multiplier = 2;
    custom.supported_items.insert(NSID("#minecraft:enchantable/equippable"));
    bool added = ctx.add_enchantment(custom);
    expect(added, "add custom enchantment");

    expect(ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom enchantment findable after add");

    // ── Modify enchantment ──
    EnchInfo patch;
    patch.max_level = 5;
    bool modded = ctx.modify_enchantment("custom:test_ench", patch);
    expect(modded, "modify custom enchantment");
    // Verify still findable after modify
    expect(ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom enchantment still findable after modify");

    // ── Remove enchantment ──
    bool removed = ctx.remove_enchantment("custom:test_ench");
    expect(removed, "remove custom enchantment");
    expect(!ctx.enchantments().contains(NSID("custom:test_ench")),
           "custom enchantment gone after remove");

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
// Test: default profiles-dir scan + activate → effective view
// ---------------------------------------------------------------------------

void test_besq_default_profiles_scan() {
    // Temporary directory containing a single profile that depends on vanilla.
    auto tmp = std::filesystem::temp_directory_path() / "besq_profiles_scan";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "moda.json");
        f << R"({"name":"moda","dependencies":["builtin:vanilla"],"enchantments":[{"id":"mod:x","name":"X","platform":"java","max_level":3,"multiplier":2,"supported_items":["#minecraft:swords"]}]})";
    }

    BesqContext ctx;
    ctx.set_profiles_dir(tmp.string());
    ctx.load_profiles();
    ctx.activate_profile("moda");
    expect(ctx.enchantments().contains(NSID("mod:x")),
           "active profile effective view loaded");

    std::filesystem::remove_all(tmp);
    TEST_PASS("BesqContext default profiles scan");
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
// Test: import_registry invalidates the effective-view cache
// ---------------------------------------------------------------------------

void test_besq_import_invalidates_effective_cache() {
    BesqContext ctx;
    ctx.load_builtin();

    // Prime the effective-view cache.
    expect(ctx.enchantments().contains(NSID("minecraft:sharpness")),
           "effective cache primed");

    // import_registry mutates the active profile directly (bypassing manager
    // _mutate); the effective-view cache must be invalidated so the imported
    // enchantment is visible on the next read.
    auto tmp = std::filesystem::temp_directory_path() / "besq_import_cache.json";
    {
        std::ofstream f(tmp);
        f << R"({"name":"extra","enchantments":[{"id":"extra:y","name":"Y","platform":"java","max_level":2,"multiplier":1,"supported_items":["#minecraft:swords"]}]})";
    }
    ctx.import_registry(tmp.string());
    std::filesystem::remove(tmp);

    expect(ctx.enchantments().contains(NSID("extra:y")),
           "imported enchantment visible via effective view");

    TEST_PASS("BesqContext import invalidates effective cache");
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
    expect(std::string(active) == "builtin:vanilla", "c abi default profile");

    int out_count = 0;
    char** profiles = besq_list_profiles(ctx, &out_count);
    expect(profiles != nullptr, "c abi list_profiles should return non-null");
    expect(out_count == 1, "c abi one profile");
    besq_free_string_list(profiles, out_count);

    rc = besq_fork_profile(ctx, "builtin:vanilla", "minecraft:testing");
    expect(rc == 0, "c abi fork_profile");
    rc = besq_activate_profile(ctx, "minecraft:testing");
    expect(rc == 0, "c abi activate_profile");
    active = besq_active_profile(ctx);
    expect(std::string(active) == "minecraft:testing", "c abi active profile testing");

    rc = besq_activate_profile(ctx, "builtin:vanilla");
    expect(rc == 0, "c abi activate default back");
    rc = besq_remove_profile(ctx, "minecraft:testing");
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
// Test: C ABI solve — omitted algorithm must resolve to a real strategy
// ---------------------------------------------------------------------------

void test_c_abi_solve_default_algo() {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    int rc = besq_load_builtin(ctx);
    expect(rc == 0, "c abi load_builtin");

    // Omit "algorithm" entirely → default "dp_merge" must be a registered
    // strategy (the old "greedy" fallback errored with unknown_algo).
    const char* json_input =
        "{\"target\":{\"equipment\":\"diamond_sword\",\"enchantments\":[{\"id\":\"sharpness\",\"level\":3}]},"
        "\"max_solutions\":1}";
    char* json_result = besq_solve(ctx, json_input);
    if (!json_result) {
        const char* err = besq_last_error(ctx);
        std::cerr << "C ABI solve (default algo) error: " << (err ? err : "null") << std::endl;
    }
    expect(json_result != nullptr, "c abi solve with omitted algorithm should succeed");
    if (json_result) {
        std::string result(json_result);
        expect(result.find("\"success\": true") != std::string::npos,
               "c abi solve (default algo) should report success");
        expect(result.find("\"algorithm\": \"dp_merge\"") != std::string::npos,
               "c abi solve (default algo) should use dp_merge");
        expect(result.find("\"solutions\"") != std::string::npos,
               "c abi solve (default algo) returns solutions");
        besq_free_string(json_result);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve default algorithm");
}

// ---------------------------------------------------------------------------
// Test: C ABI solve — inventory mode parses the "items" array
// ---------------------------------------------------------------------------

void test_c_abi_solve_inventory() {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    int rc = besq_load_builtin(ctx);
    expect(rc == 0, "c abi load_builtin");

    // Inventory mode with a real items array.  "algorithm" is omitted so the
    // mode-dependent default (hamming) is exercised, and the items are parsed
    // and fed to the solver (previously the payload was always empty).
    const char* json_input =
        "{\"target\":{\"equipment\":\"diamond_sword\",\"enchantments\":[{\"id\":\"sharpness\",\"level\":5}]},"
        "\"mode\":\"inventory\","
        "\"items\":["
        "{\"type\":\"book\",\"enchants\":[{\"id\":\"sharpness\",\"level\":5}],\"prior_penalty\":0,\"priority\":1},"
        "{\"type\":\"book\",\"enchants\":[{\"id\":\"knockback\",\"level\":2}],\"prior_penalty\":0,\"priority\":2},"
        "{\"type\":\"equipment\",\"id\":\"diamond_sword\"}"
        "],"
        "\"max_solutions\":1}";
    char* json_result = besq_solve(ctx, json_input);
    if (!json_result) {
        const char* err = besq_last_error(ctx);
        std::cerr << "C ABI solve (inventory) error: " << (err ? err : "null") << std::endl;
    }
    expect(json_result != nullptr, "c abi solve inventory mode should succeed");
    if (json_result) {
        std::string result(json_result);
        expect(result.find("\"success\": true") != std::string::npos,
               "c abi inventory solve should report success");
        expect(result.find("\"algorithm\": \"hamming\"") != std::string::npos,
               "c abi inventory solve should default to hamming");
        expect(result.find("\"solutions\"") != std::string::npos,
               "c abi inventory solve returns solutions");
        besq_free_string(json_result);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve inventory mode");
}

// ---------------------------------------------------------------------------
// Test: C ABI solve — unknown enchantment id must error (not silently drop)
// ---------------------------------------------------------------------------

void test_c_abi_solve_unknown_ench() {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    int rc = besq_load_builtin(ctx);
    expect(rc == 0, "c abi load_builtin");

    // Unknown enchant id in the source → parse_ench_set must throw instead of
    // silently dropping the enchantment.
    const char* json_input =
        "{\"target\":{\"equipment\":\"diamond_sword\",\"enchantments\":[{\"id\":\"sharpness\",\"level\":3}]},"
        "\"source\":[{\"id\":\"nonexistent_ench\",\"level\":1}],"
        "\"max_solutions\":1}";
    char* json_result = besq_solve(ctx, json_input);
    expect(json_result == nullptr, "c abi solve with unknown enchant id should error");
    if (json_result) besq_free_string(json_result);
    const char* err = besq_last_error(ctx);
    expect(err != nullptr, "c abi solve unknown ench should set last_error");
    if (err) {
        std::string msg(err);
        expect(msg.find("unknown") != std::string::npos ||
                   msg.find("cli.err.unknown_ench") != std::string::npos,
               "c abi solve unknown ench error mentions unknown enchant");
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve unknown enchant");
}

// ---------------------------------------------------------------------------
// Test: concurrent abort_solve (B-T22)
// ---------------------------------------------------------------------------

void test_besq_abort_concurrent() {
    // B-T22: BesqContext::active_executor is now an atomic shared_ptr handle.
    // Calling abort_solve() from another thread while solve() runs must be
    // safe — the old raw executor pointer was raced (data race, UB) and could
    // dangle (use-after-free) when stage_execute destroyed the executor.
    BesqContext ctx;
    ctx.load_builtin();

    // Register custom sword enchantments so we can build a target large enough
    // that dp_merge's exponential search stays in-flight while abort_solve()
    // fires from the other thread.
    constexpr int kEnchCount = 20;
    for (int i = 0; i < kEnchCount; ++i) {
        EnchInfo info;
        info.id = NSID("test:ench_" + std::to_string(i));
        info.name = "Test Ench " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(ctx.add_enchantment(info),
               "add custom enchantment test:ench_" + std::to_string(i));
    }

    // Target: diamond_sword with all custom enchants at level 5, no source.
    EnchSet target_enchs;
    for (int i = 0; i < kEnchCount; ++i)
        target_enchs.emplace(NSID("test:ench_" + std::to_string(i)), 5);
    Item target_item;
    target_item.id = NSID("minecraft:diamond_sword");
    target_item.enchantments = target_enchs;
    if (auto eq_it = ctx.equipment().find(NSID("minecraft:diamond_sword"));
        eq_it != ctx.equipment().end()) {
        target_item.durability = eq_it->max_durability;
    }

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{};
    request.algorithm = "dp_merge";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;
    // Safety bound: if abort_solve() somehow fails to cancel, the executor's
    // timeout watcher cancels after this instead of running forever.
    request.search_config.max_search_time = std::chrono::milliseconds(1500);

    SolveResult result;
    std::string error;
    std::atomic<bool> done{false};
    std::thread solver([&] {
        try {
            result = ctx.solve(request);
        } catch (const std::exception& e) {
            error = e.what();
        }
        done = true;
    });

    // Let the solve publish the executor handle, then hammer abort_solve()
    // until the solve finishes.  This overlaps the abort thread's load with
    // the solve thread's store/clear of the shared handle.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    while (!done.load()) {
        ctx.abort_solve();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    solver.join();

    expect(error.empty(), "concurrent solve must not throw (got: " + error + ")");
    // A cancelled solve reports success=false with no solutions; a solve that
    // slipped through before the abort reports success=true with solutions.
    // Either outcome is fine — the invariant is the two must never disagree
    // (and there must be no crash / UB).
    expect(!result.success || !result.solutions.empty(),
           "completed solve must produce solutions");
    expect(result.success || result.solutions.empty(),
           "cancelled solve must report no solutions");
    TEST_PASS("BesqContext concurrent abort (B-T22)");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        test_context_lifecycle();
        test_fork_merge();
        test_besq_solve();
        test_besq_solve_already_met();
        test_besq_registry_edit();
        test_besq_default_profiles_scan();
        test_besq_export();
        test_besq_import_invalidates_effective_cache();
        test_c_abi();
        test_c_abi_solve_default_algo();
        test_c_abi_solve_inventory();
        test_c_abi_solve_unknown_ench();
        test_besq_abort_concurrent();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }

    return print_summary();
}
