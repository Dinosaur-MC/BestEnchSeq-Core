// =============================================================================
// BesqContext Acceptance Tests
//
// Tests the public C++ API: profile lifecycle, registry editing, solve pipeline.
// =============================================================================

#define BESQ_TEST_MAIN
#include "besq/besq.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/ItemParser.h"
#include "framework/test_framework.h"

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

TEST_CASE("test_context_lifecycle") {
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

TEST_CASE("test_fork_merge") {
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

    expect(ctx.enchantments().contains(NSID("custom:test_ench")), "custom ench exists in modded after add");

    // Default profile should NOT have custom
    ctx.activate_profile("builtin:vanilla");
    expect(!ctx.enchantments().contains(NSID("custom:test_ench")), "custom ench NOT in default profile");

    // Merge modded -> default
    ctx.merge_profile("minecraft:modded", "builtin:vanilla");
    expect(ctx.enchantments().contains(NSID("custom:test_ench")), "custom ench merged to default");

    // Cleanup
    ctx.remove_profile("minecraft:modded");

    TEST_PASS("BesqContext fork/merge correctness");
}

// ---------------------------------------------------------------------------
// Test: Solve via BesqContext
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_solve") {
    BesqContext ctx;
    ctx.load_builtin();

    // Use inline target syntax: diamond_sword[sharpness=3]
    std::string target_str = "diamond_sword[sharpness=3]";
    const char* argv[] = {"besq", "--target", target_str.c_str()};
    auto config = CLIApp::parse(3, const_cast<char**>(argv));

    // Parse target item using the context's registries
    Item target_item = ItemParser::parse(config.target, ctx.enchantments(), ctx.equipment());

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
    expect(json_out.find("sharpness") != std::string::npos, "JSON output should mention sharpness");

    auto text_out = ctx.format(result, AlgorithmMode::direct, "text");
    expect(!text_out.empty(), "text output should be non-empty");

    TEST_PASS("BesqContext solve + format");
}

// ---------------------------------------------------------------------------
// Test: Solve when source already satisfies the target (goal already met)
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_solve_already_met") {
    BesqContext ctx;
    ctx.load_builtin();

    // Source == target: the current state already meets the goal.  This must
    // produce a 0-step solution, NOT "目标不可达".
    const char* argv[] = {"besq", "--target", "diamond_sword[sharpness=5]", "--source", "sharpness=5"};
    auto config = CLIApp::parse(5, const_cast<char**>(argv));

    Item target_item = ItemParser::parse(config.target, ctx.enchantments(), ctx.equipment());

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{EnchParser::parse(config.source, ctx.enchantments())};
    request.algorithm = "dp_merge";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 1;

    auto result = ctx.solve(request);
    expect(result.success, "already-met solve should succeed (not unreachable)");
    expect(!result.solutions.empty(), "already-met solve should produce a solution");
    expect(result.solutions[0].is_success, "already-met solution should succeed");
    expect(result.solutions[0].steps.empty(), "already-met solve should be 0 steps");
    expect(result.solutions[0].total_exp_level_cost == 0, "already-met solve cost should be 0");

    auto text_out = ctx.format(result, AlgorithmMode::direct, "text");
    expect(!text_out.empty(), "already-met text output should be non-empty");

    TEST_PASS("BesqContext solve already-met");
}

// ---------------------------------------------------------------------------
// Test: Registry editing (add / modify / remove)
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_registry_edit") {
    BesqContext ctx;
    ctx.load_builtin();

    // Verify known enchantments exist
    expect(ctx.enchantments().contains(NSID("minecraft:sharpness")), "sharpness should exist in builtin data");

    expect(ctx.equipment().contains(NSID("minecraft:diamond_sword")), "diamond_sword should exist");

    // ── Add custom enchantment ──
    EnchInfo custom;
    custom.id = NSID("custom:test_ench");
    custom.name = "Test Enchantment";
    custom.max_level = 3;
    custom.multiplier = 2;
    custom.supported_items.insert(NSID("#minecraft:enchantable/equippable"));
    bool added = ctx.add_enchantment(custom);
    expect(added, "add custom enchantment");

    expect(ctx.enchantments().contains(NSID("custom:test_ench")), "custom enchantment findable after add");

    // ── Modify enchantment ──
    EnchInfo patch;
    patch.max_level = 5;
    bool modded = ctx.modify_enchantment("custom:test_ench", patch);
    expect(modded, "modify custom enchantment");
    // Verify still findable after modify
    expect(ctx.enchantments().contains(NSID("custom:test_ench")), "custom enchantment still findable after modify");

    // ── Remove enchantment ──
    bool removed = ctx.remove_enchantment("custom:test_ench");
    expect(removed, "remove custom enchantment");
    expect(!ctx.enchantments().contains(NSID("custom:test_ench")), "custom enchantment gone after remove");

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

TEST_CASE("test_besq_default_profiles_scan") {
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
    expect(ctx.enchantments().contains(NSID("mod:x")), "active profile effective view loaded");

    std::filesystem::remove_all(tmp);
    TEST_PASS("BesqContext default profiles scan");
}

// ---------------------------------------------------------------------------
// Test: auto_load — domain-wide auto-load entry (built-in → profiles →
// algorithms → langs; conflict rules owned by AutoLoadPipeline)
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_auto_load") {
    // Temporary directory with one external profile (depends on vanilla).
    auto tmp = std::filesystem::temp_directory_path() / "besq_auto_load";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "modauto.json");
        f << R"({"name":"modauto","dependencies":["builtin:vanilla"],"enchantments":[{"id":"mod:auto","name":"Auto","platform":"java","max_level":1,"multiplier":1,"supported_items":["#minecraft:swords"]}]})";
    }

    BesqContext ctx;
    ctx.set_profiles_dir(tmp.string());  // honored by auto_load
    ctx.auto_load();

    // 1. Built-in FIRST: root present and activated.
    expect(ctx.active_profile() == "builtin:vanilla", "auto_load activates builtin:vanilla");
    // 2. External profile loaded AFTER built-in (same-key replace would also
    //    be honored — see ProfileManager::load_directory).
    bool has_root = false, has_mod = false;
    for (const auto& p : ctx.list_profiles()) {
        if (p == "builtin:vanilla")
            has_root = true;
        if (p == "modauto")
            has_mod = true;
    }
    expect(has_root && has_mod, "auto_load loads builtin + external profile");
    ctx.activate_profile("modauto");
    expect(ctx.enchantments().contains(NSID("mod:auto")), "auto-loaded profile effective view usable");

    // 3. Algorithms: built-ins kept (plugin dir missing → silent 0).
    bool has_dp = false;
    for (const auto& a : ctx.list_algorithms())
        if (a == "dp_merge")
            has_dp = true;
    expect(has_dp, "auto_load keeps builtin algorithms");

    std::filesystem::remove_all(tmp);
    TEST_PASS("BesqContext auto_load");
}

// ---------------------------------------------------------------------------
// Test: effective_profile — dependency-merged effective view accessor
// ---------------------------------------------------------------------------

TEST_CASE("test_effective_profile_view") {
    // Scaffold a dependent profile in a temp dir (dependency content must be
    // merged into the effective view).
    auto tmp = std::filesystem::temp_directory_path() / "besq_effective_profile";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "modd.json");
        f << R"({"name":"modd","dependencies":["builtin:vanilla"],"enchantments":[{"id":"mod:eff","name":"Eff","platform":"java","max_level":2,"multiplier":1,"supported_items":["#minecraft:swords"]}]})";
    }

    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp.string());
    ctx.load_profiles();

    // Root profile (no deps): effective view == own view.
    const Profile& root = ctx.effective_profile("builtin:vanilla");
    expect(root.ench().contains(NSID("minecraft:sharpness")), "root effective view has vanilla enchantments");
    expect(root.tag_resolver() != nullptr, "effective view attaches a tag resolver");

    // Dependent profile: own content + dependency content merged.
    const Profile& eff = ctx.effective_profile("modd");
    expect(eff.ench().contains(NSID("mod:eff")), "own enchantment in effective view");
    expect(eff.ench().contains(NSID("minecraft:sharpness")), "dependency content merged into effective view");
    expect(eff.eq().contains(NSID("minecraft:diamond_sword")), "dependency equipment merged into effective view");
    expect(eff.tag_resolver() != nullptr, "dependent effective view attaches a tag resolver");

    // Unknown profile → std::runtime_error (accessor contract; the underlying
    // resolve_effective would silently return an empty view).
    bool threw = false;
    try {
        (void)ctx.effective_profile("nope");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "unknown profile throws std::runtime_error");

    std::filesystem::remove_all(tmp);
    TEST_PASS("BesqContext effective_profile view");
}

// ---------------------------------------------------------------------------
// Test: Export profile
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_export") {
    BesqContext ctx;
    ctx.load_builtin();

    const std::string test_path = "besq_ctx_test_export.json";
    bool ok = ctx.export_profile(test_path);
    expect(ok, "export should succeed");

    if (std::filesystem::exists(test_path)) {
        expect(std::filesystem::file_size(test_path) > 0, "export file should not be empty");
        std::filesystem::remove(test_path);
    }

    TEST_PASS("BesqContext export");
}

// ---------------------------------------------------------------------------
// Test: Composite profile activation — active_profiles / composite_active /
// activate_profile_group / _resolve_active drives the read-only views
// ---------------------------------------------------------------------------

TEST_CASE("test_ctx_group_activate_and_resolve") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.fork_profile("builtin:vanilla", "group_a");
    ctx.fork_profile("builtin:vanilla", "group_b");
    ctx.add_enchantment_to("group_a", EnchInfo{NSID("mod:ember"), "Ember", MCE::All, 3, 3, 2, false,
                                               {}, {NSID("#minecraft:swords")}});
    ctx.add_equipment_to("group_b", Equipment{NSID("mod:ember_boots"), "Ember Boots",
                                              NSID("#minecraft:boots"), 143});

    expect(ctx.active_profiles().size() == 1, "single profile before group activation");
    expect(!ctx.composite_active(), "not composite before group activation");

    ctx.activate_profile_group({"group_a", "group_b"});
    expect(ctx.composite_active(), "composite active after group activation");
    expect(ctx.active_profiles() == std::vector<std::string>({"group_a", "group_b"}),
           "active_profiles returns members in order");
    expect(ctx.active_profile() == "group_a", "single-value accessor returns the FIRST member");
    expect(ctx.enchantments().contains(NSID("mod:ember")), "group view includes group_a enchant");
    expect(ctx.equipment().contains(NSID("mod:ember_boots")), "group view includes group_b equipment");
    expect(ctx.enchantments().contains(NSID("minecraft:sharpness")), "implicit vanilla base present in group view");

    // 单 profile 激活清除组合。
    ctx.activate_profile("group_a");
    expect(!ctx.composite_active(), "single activation clears the group");
    expect(!ctx.equipment().contains(NSID("mod:ember_boots")),
           "group_b equipment gone after switching to single profile");

    // 组合成员不存在 → throw。
    bool threw = false;
    try {
        ctx.activate_profile_group({"group_a", "missing"});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "group activation with unknown member throws");

    TEST_PASS("test_ctx_group_activate_and_resolve");
}

// ---------------------------------------------------------------------------
// Test: import_profile invalidates the effective-view cache
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_import_profile_invalidates_effective_cache") {
    BesqContext ctx;
    ctx.load_builtin();

    // Prime the effective-view cache.
    expect(ctx.enchantments().contains(NSID("minecraft:sharpness")), "effective cache primed");

    // import_profile mutates the active profile directly (bypassing manager
    // _mutate); the effective-view cache must be invalidated so the imported
    // enchantment is visible on the next read.
    auto tmp = std::filesystem::temp_directory_path() / "besq_import_cache.json";
    {
        std::ofstream f(tmp);
        f << R"({"name":"extra","enchantments":[{"id":"extra:y","name":"Y","platform":"java","max_level":2,"multiplier":1,"supported_items":["#minecraft:swords"]}]})";
    }
    ctx.import_profile(tmp.string());
    std::filesystem::remove(tmp);

    expect(ctx.enchantments().contains(NSID("extra:y")), "imported enchantment visible via effective view");

    TEST_PASS("BesqContext import invalidates effective cache");
}

// ---------------------------------------------------------------------------
// Test: load_file on a datapack dir keeps #mypack:* enchantments (#24)
// ---------------------------------------------------------------------------
// FormatDetector used to drop a datapack's item_tags, so an enchantment whose
// supported_items references the datapack's own `#mypack:*` tag was silently
// removed during cross-validation.  The datapack item tags now seed the
// validation universe and land in the active profile's tag registry.

TEST_CASE("test_besq_load_file_datapack_keeps_tags") {
    auto dir = std::filesystem::temp_directory_path() / "besq_load_file_dp";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mypack" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "mypack" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "enchantment" / "leeching.json");
        f << R"({"supported_items": "#mypack:swords", "anvil_cost": 2, "max_level": 3})";
    }

    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_file(dir.string());

    expect(ctx.enchantments().contains(NSID("mypack:leeching")), "load_file on a datapack keeps the #mypack:* enchantment");
    expect(ctx.categories().contains(NSID("#mypack:swords")), "datapack item tag lands in the active profile's tag registry");

    std::filesystem::remove_all(dir);
    TEST_PASS("BesqContext load_file datapack keeps tags");
}

// ---------------------------------------------------------------------------
// Test: C ABI bindings
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi") {
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
        expect(result.find("solutions") != std::string::npos, "c abi solve returns solutions");
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
    rc = besq_export_profile(ctx, "besq_abi_test_export.json");
    expect(rc == 0, "c abi export_profile");
    if (std::filesystem::exists("besq_abi_test_export.json")) {
        expect(std::filesystem::file_size("besq_abi_test_export.json") > 0, "c abi export file not empty");
        std::filesystem::remove("besq_abi_test_export.json");
    }

    besq_destroy(ctx);

    TEST_PASS("BesqContext C ABI");
}

// ---------------------------------------------------------------------------
// Test: C ABI solve — omitted algorithm must resolve to a real strategy
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_solve_default_algo") {
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
        expect(result.find("\"success\": true") != std::string::npos, "c abi solve (default algo) should report success");
        expect(result.find("\"algorithm\": \"dp_merge\"") != std::string::npos,
               "c abi solve (default algo) should use dp_merge");
        expect(result.find("\"solutions\"") != std::string::npos, "c abi solve (default algo) returns solutions");
        besq_free_string(json_result);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve default algorithm");
}

// ---------------------------------------------------------------------------
// Test: C ABI solve — inventory mode parses the "items" array
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_solve_inventory") {
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
        expect(result.find("\"success\": true") != std::string::npos, "c abi inventory solve should report success");
        expect(result.find("\"algorithm\": \"hamming\"") != std::string::npos,
               "c abi inventory solve should default to hamming");
        expect(result.find("\"solutions\"") != std::string::npos, "c abi inventory solve returns solutions");
        besq_free_string(json_result);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve inventory mode");
}

// ---------------------------------------------------------------------------
// Test: C ABI solve — unknown enchantment id must error (not silently drop)
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_solve_unknown_ench") {
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
    if (json_result)
        besq_free_string(json_result);
    const char* err = besq_last_error(ctx);
    expect(err != nullptr, "c abi solve unknown ench should set last_error");
    if (err) {
        std::string msg(err);
        expect(msg.find("unknown") != std::string::npos || msg.find("cli.err.unknown_ench") != std::string::npos,
               "c abi solve unknown ench error mentions unknown enchant");
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI solve unknown enchant");
}

// ---------------------------------------------------------------------------
// Test: C ABI version
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_version") {
    const char* v = besq_get_version();
    expect(v != nullptr, "c abi get_version: non-null");
    expect(v && *v != '\0', "c abi get_version: non-empty");
    TEST_PASS("C ABI version");
}

// ---------------------------------------------------------------------------
// Helper: write a small native profile JSON (a custom enchantment) to a temp
// file and return the path.  Removed on scope exit.
// ---------------------------------------------------------------------------
class TempProfileFile {
public:
    explicit TempProfileFile(const std::string& content) {
        static int counter = 0;
        _path = (std::filesystem::temp_directory_path() / ("besq_abi_prof_" + std::to_string(++counter) + ".json")).string();
        std::ofstream f(_path);
        f << content;
    }
    ~TempProfileFile() {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }
    const char* c_str() const { return _path.c_str(); }

private:
    std::string _path;
};

// ---------------------------------------------------------------------------
// Test: C ABI load_file — merges the file's enchantments into the active
// profile (visible via besq_list_enchantments)
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_load_file") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    TempProfileFile f(
        R"({"name":"abi_extra","enchantments":[{"id":"mod:cabi","name":"Cabi","platform":"java","max_level":3,"multiplier":2,"supported_items":["#minecraft:swords"]}]})");
    int rc = besq_load_file(ctx, f.c_str());
    expect_eq(rc, 0, "c abi load_file returns 0");

    char* list = besq_list_enchantments(ctx);
    expect(list != nullptr, "c abi list_enchantments non-null after load_file");
    if (list) {
        expect(std::string(list).find("mod:cabi") != std::string::npos,
               "c abi load_file merges the enchantment into the active profile");
        besq_free_string(list);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI load_file");
}

// ---------------------------------------------------------------------------
// Test: C ABI load_data — same merge path via the filters routing
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_load_data") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    TempProfileFile f(
        R"({"name":"abi_data","enchantments":[{"id":"mod:cabid","name":"CabiD","platform":"java","max_level":2,"multiplier":1,"supported_items":["#minecraft:swords"]}]})");
    int rc = besq_load_data(ctx, f.c_str());
    expect_eq(rc, 0, "c abi load_data returns 0");

    char* list = besq_list_enchantments(ctx);
    expect(list != nullptr, "c abi list_enchantments non-null after load_data");
    if (list) {
        expect(std::string(list).find("mod:cabid") != std::string::npos,
               "c abi load_data merges the enchantment into the active profile");
        besq_free_string(list);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI load_data");
}

// ---------------------------------------------------------------------------
// Test: C ABI import_profile — merges into the active profile
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_import_profile") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    TempProfileFile f(
        R"({"name":"abi_import","enchantments":[{"id":"mod:cabiimp","name":"CabiImp","platform":"java","max_level":2,"multiplier":1,"supported_items":["#minecraft:swords"]}]})");
    int rc = besq_import_profile(ctx, f.c_str());
    expect_eq(rc, 0, "c abi import_profile returns 0");

    char* list = besq_list_enchantments(ctx);
    expect(list != nullptr, "c abi list_enchantments non-null after import_profile");
    if (list) {
        expect(std::string(list).find("mod:cabiimp") != std::string::npos,
               "c abi import_profile merges the enchantment into the active profile");
        besq_free_string(list);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI import_profile");
}

// ---------------------------------------------------------------------------
// Test: C ABI merge_profile — content added to source lands in dest
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_merge_profile") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    expect(besq_fork_profile(ctx, "builtin:vanilla", "minecraft:src") == 0, "fork src");
    expect(besq_fork_profile(ctx, "builtin:vanilla", "minecraft:dst") == 0, "fork dst");
    expect(besq_activate_profile(ctx, "minecraft:src") == 0, "activate src");

    int rc = besq_add_enchantment(
        ctx, R"({"id":"mod:mrg","name":"Mrg","max_level":3,"multiplier":2,"supported_items":["#minecraft:swords"]})");
    expect_eq(rc, 0, "c abi add_enchantment to src");

    expect(besq_merge_profile(ctx, "minecraft:src", "minecraft:dst") == 0, "merge src into dst");
    expect(besq_activate_profile(ctx, "minecraft:dst") == 0, "activate dst");

    char* list = besq_list_enchantments(ctx);
    expect(list != nullptr, "c abi list_enchantments non-null after merge");
    if (list) {
        expect(std::string(list).find("mod:mrg") != std::string::npos, "c abi merged enchantment is present in dest");
        besq_free_string(list);
    }

    besq_remove_profile(ctx, "minecraft:src");
    besq_remove_profile(ctx, "minecraft:dst");
    besq_destroy(ctx);
    TEST_PASS("C ABI merge_profile");
}

// ---------------------------------------------------------------------------
// Test: C ABI enchantment add / modify / remove (incl. error paths)
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_ench_edit") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    int rc = besq_add_enchantment(
        ctx, R"({"id":"mod:edit","name":"Edit","max_level":3,"multiplier":2,"supported_items":["#minecraft:swords"]})");
    expect_eq(rc, 0, "c abi add_enchantment");

    rc = besq_add_enchantment(
        ctx, R"({"id":"mod:edit","name":"Edit","max_level":3,"multiplier":2,"supported_items":["#minecraft:swords"]})");
    expect_eq(rc, -1, "c abi duplicate add_enchantment fails");
    expect(besq_last_error(ctx) != nullptr, "c abi last_error set on duplicate add");

    rc = besq_modify_enchantment(ctx, R"({"id":"mod:edit","max_level":5})");
    expect_eq(rc, 0, "c abi modify_enchantment");

    rc = besq_modify_enchantment(ctx, R"({"id":"mod:nope","max_level":5})");
    expect_eq(rc, -1, "c abi modify nonexistent enchantment fails");

    rc = besq_remove_enchantment(ctx, "mod:edit");
    expect_eq(rc, 0, "c abi remove_enchantment");

    rc = besq_remove_enchantment(ctx, "mod:edit");
    expect_eq(rc, -1, "c abi remove nonexistent enchantment fails");

    besq_destroy(ctx);
    TEST_PASS("C ABI enchantment edit");
}

// ---------------------------------------------------------------------------
// Test: C ABI equipment add / remove (incl. error paths)
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_equipment_edit") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    int rc = besq_add_equipment(ctx, R"({"id":"mod:weapon","name":"Weapon","category":"sword","max_durability":1561})");
    expect_eq(rc, 0, "c abi add_equipment");

    rc = besq_add_equipment(ctx, R"({"id":"mod:weapon","name":"Weapon","category":"sword","max_durability":1561})");
    expect_eq(rc, -1, "c abi duplicate add_equipment fails");

    rc = besq_remove_equipment(ctx, "mod:weapon");
    expect_eq(rc, 0, "c abi remove_equipment");

    rc = besq_remove_equipment(ctx, "mod:weapon");
    expect_eq(rc, -1, "c abi remove nonexistent equipment fails");

    besq_destroy(ctx);
    TEST_PASS("C ABI equipment edit");
}

// ---------------------------------------------------------------------------
// Test: C ABI add_category + list_categories
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_category") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    int rc = besq_add_category(ctx, "mycat");
    expect_eq(rc, 0, "c abi add_category");

    rc = besq_add_category(ctx, "mycat");
    expect_eq(rc, -1, "c abi duplicate add_category fails");

    char* cats = besq_list_categories(ctx);
    expect(cats != nullptr, "c abi list_categories non-null");
    if (cats) {
        expect(std::string(cats).find("mycat") != std::string::npos, "c abi list_categories contains the added category");
        besq_free_string(cats);
    }

    besq_destroy(ctx);
    TEST_PASS("C ABI category");
}

// ---------------------------------------------------------------------------
// Test: C ABI list_enchantments / list_equipment / list_categories
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_lists") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    char* ench = besq_list_enchantments(ctx);
    expect(ench != nullptr, "c abi list_enchantments non-null");
    if (ench) {
        expect(std::string(ench).find("sharpness") != std::string::npos, "c abi list_enchantments has sharpness");
        besq_free_string(ench);
    }

    char* eq = besq_list_equipment(ctx);
    expect(eq != nullptr, "c abi list_equipment non-null");
    if (eq) {
        expect(std::string(eq).find("diamond_sword") != std::string::npos, "c abi list_equipment has diamond_sword");
        besq_free_string(eq);
    }

    char* cat = besq_list_categories(ctx);
    expect(cat != nullptr, "c abi list_categories non-null");
    if (cat)
        besq_free_string(cat);

    besq_destroy(ctx);
    TEST_PASS("C ABI lists");
}

// ---------------------------------------------------------------------------
// Test: C ABI list_algorithms — built-in strategies enumerated
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_list_algorithms") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    int n = 0;
    char** algos = besq_list_algorithms(ctx, &n);
    expect(algos != nullptr, "c abi list_algorithms non-null");
    expect(n >= 3, "c abi list_algorithms has at least the 3 built-ins");
    bool has_dp = false;
    for (int i = 0; i < n; ++i)
        if (algos[i] && std::string(algos[i]) == "dp_merge")
            has_dp = true;
    expect(has_dp, "c abi list_algorithms contains dp_merge");
    besq_free_string_list(algos, n);

    besq_destroy(ctx);
    TEST_PASS("C ABI list_algorithms");
}

// ---------------------------------------------------------------------------
// Test: C ABI abort_solve when idle is a safe no-op
// ---------------------------------------------------------------------------

TEST_CASE("test_c_abi_abort_idle") {
    auto* ctx = besq_create();
    expect(ctx != nullptr, "c abi create");
    expect(besq_load_builtin(ctx) == 0, "c abi load_builtin");

    int rc = besq_abort_solve(ctx);
    expect_eq(rc, 0, "c abi abort_solve when idle is a no-op");

    besq_destroy(ctx);
    TEST_PASS("C ABI abort idle");
}

// ---------------------------------------------------------------------------
// Test: concurrent abort_solve (B-T22)
// ---------------------------------------------------------------------------

TEST_CASE("test_besq_abort_concurrent") {
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
        expect(ctx.add_enchantment(info), "add custom enchantment test:ench_" + std::to_string(i));
    }

    // Target: diamond_sword with all custom enchants at level 5, no source.
    EnchSet target_enchs;
    for (int i = 0; i < kEnchCount; ++i)
        target_enchs.emplace(NSID("test:ench_" + std::to_string(i)), 5);
    Item target_item;
    target_item.id = NSID("minecraft:diamond_sword");
    target_item.enchantments = target_enchs;
    if (auto eq_it = ctx.equipment().find(NSID("minecraft:diamond_sword")); eq_it != ctx.equipment().end()) {
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
    expect(!result.success || !result.solutions.empty(), "completed solve must produce solutions");
    expect(result.success || result.solutions.empty(), "cancelled solve must report no solutions");
    TEST_PASS("BesqContext concurrent abort (B-T22)");
}

// ---------------------------------------------------------------------------
// Test: Named-profile editing variants + metadata + rename
// ---------------------------------------------------------------------------

TEST_CASE("test_named_profile_edit_and_meta") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    std::string active = ctx.active_profile();
    auto meta = ctx.profile_metadata(active);
    expect(meta.name == active, "meta name");
    expect(meta.is_root, "builtin root flagged");
    expect(meta.ench_count == ctx.profile(active).ench().size(), "ench count");
    expect(meta.eq_count == ctx.profile(active).eq().size(), "eq count");

    // rename round-trip
    std::string new_name = active + "_renamed";
    if (!ctx.profile_exists(new_name)) {
        expect(ctx.rename_profile(active, new_name), "rename");
        expect(ctx.profile_exists(new_name) && !ctx.profile_exists(active), "renamed maps");
        expect(ctx.profile_metadata(new_name).name == new_name, "metadata reflects new identity");
    }

    TEST_PASS("BesqContext named profile edit + metadata");
}

// ---------------------------------------------------------------------------
// Test: add_enchantment_to / remove_enchantment_from facade (by-name CRUD)
// ---------------------------------------------------------------------------

TEST_CASE("test_facade_by_name_registry") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    std::string key = ctx.list_profiles()[0];

    // Add a brand-new enchantment to the named profile via the facade.
    EnchInfo e;
    e.id = NSID("test:zz_ench");
    e.name = "Zz Test Enchantment";
    e.max_level = 1;
    e.multiplier = 2;
    e.supported_items.insert(NSID("#minecraft:swords"));
    expect(ctx.add_enchantment_to(key, e), "add ench");
    expect(ctx.profile(key).ench().find(e.id) != ctx.profile(key).ench().end(), "ench present");

    expect(ctx.remove_enchantment_from(key, e.id), "remove ench");
    expect(ctx.profile(key).ench().find(e.id) == ctx.profile(key).ench().end(), "ench gone");

    TEST_PASS("BesqContext facade by-name registry");
}

// ---------------------------------------------------------------------------
// Test: update_*_to named-profile variants (enchantment/equipment/tag)
// ---------------------------------------------------------------------------

TEST_CASE("test_update_variants") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    std::string key = ctx.list_profiles()[0];
    const auto& prof = ctx.profile(key);

    // enchantment update：取一个现存 ench 改 max_level
    if (!prof.ench().empty()) {
        auto e = *prof.ench().begin();
        e.max_level = e.max_level + 1;
        expect(ctx.update_enchantment_to(key, e), "update ench");
        expect(ctx.profile(key).ench().find(e.id)->max_level == e.max_level, "updated");
    }

    // equipment update：改 max_durability
    if (!prof.eq().empty()) {
        auto eq = *prof.eq().begin();
        eq.max_durability = eq.max_durability + 1;
        expect(ctx.update_equipment_to(key, eq), "update equipment");
        expect(ctx.profile(key).eq().find(eq.id)->max_durability == eq.max_durability, "eq updated");
    }

    // tag update：改显示名
    if (!prof.tags().empty()) {
        auto tag = *prof.tags().begin();
        tag.name = tag.name + "X";
        expect(ctx.update_tag_to(key, tag), "update tag");
        expect(ctx.profile(key).tags().find(tag.id)->name == tag.name, "tag updated");
    }

    TEST_PASS("BesqContext update variants");
}

// ---------------------------------------------------------------------------
// Test: algorithm detail + unload gate (builtin/unknown are never unloaded)
// ---------------------------------------------------------------------------

TEST_CASE("test_algorithm_detail_and_unload_gate") {
    BesqContext ctx;
    ctx.load_builtin();

    auto detail = ctx.algorithm_detail("dp_merge");
    expect(detail.name == "dp_merge", "detail name");
    expect(detail.origin == AlgorithmOrigin::builtin, "builtin origin");
    expect(detail.supported_mode == "direct" || detail.supported_mode == "inventory" || detail.supported_mode == "both",
           "supported_mode valid");

    // unload builtin → false
    expect(!ctx.unload_algorithm("dp_merge"), "cannot unload builtin");
    // 未知算法 → false
    expect(!ctx.unload_algorithm("nope"), "unknown unload false");
    // 未知算法 detail → 抛
    bool threw = false;
    try {
        (void)ctx.algorithm_detail("nope");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "unknown algorithm detail throws");

    TEST_PASS("BesqContext algorithm detail + unload gate");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
