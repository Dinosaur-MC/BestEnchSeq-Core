// =============================================================================
// CLI Acceptance Tests
//
// Tests CLI argument parsing for all parameter combinations, error handling,
// and edge cases. Validates that new features (--export without
// --target, --max-time, --edit, --config) parse correctly.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/interface/components/BuiltinI18n.h"
#include "common/i18n/Language.h"
#include "common/utils/EnvUtil.hpp"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/cli/CLIApp.h"
#include "framework/test_framework.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>

// ---------------------------------------------------------------------------
// Helper: RAII guard writing inventory JSON to a unique temp file; the file is
// removed on scope exit (no leftovers between test runs).
// ---------------------------------------------------------------------------
class TempInvFile {
public:
    explicit TempInvFile(const std::string& content) {
        static int counter = 0;
        _path = (std::filesystem::temp_directory_path() / ("besq_cli_inv_" + std::to_string(++counter) + ".json")).string();
        std::ofstream f(_path);
        f << content;
    }
    ~TempInvFile() {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }

    const char* c_str() const { return _path.c_str(); }

private:
    std::string _path;
};

// ---------------------------------------------------------------------------
// Test: --export without --target is valid
// ---------------------------------------------------------------------------

TEST_CASE("test_export_only_valid") {
    {
        const char* argv[] = {"besq", "--export", "out.json"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        expect(config.target.empty(), "target should be empty");
        expect(config.export_path.has_value(), "export_path should be set");
        expect(*config.export_path == "out.json", "path should match");
        TEST_PASS("--export only (no --target)");
    }
    {
        const char* argv[] = {"besq", "--export", "out.csv", "--verbose"};
        auto config = CLIApp::parse(4, const_cast<char**>(argv));
        expect(config.target.empty(), "target should be empty with --verbose");
        expect(config.verbose, "verbose should be set");
        expect(config.export_path.has_value(), "export_path should be set");
        TEST_PASS("--export + --verbose (no --target)");
    }
}

// ---------------------------------------------------------------------------
// Test: Missing both --target and --export is an error
// ---------------------------------------------------------------------------

TEST_CASE("test_missing_target_and_export_errors") {
    {
        const char* argv[] = {"besq", "--algorithm", "greedy"};
        expect_throws([&] { CLIApp::parse(3, const_cast<char**>(argv)); },
                      "Must throw when both --target and --export missing");
        TEST_PASS("no --target and no --export throws");
    }
    {
        const char* argv[] = {"besq", "--verbose", "--format", "json"};
        expect_throws([&] { CLIApp::parse(4, const_cast<char**>(argv)); }, "Must throw with flags only, no target");
        TEST_PASS("flags only (no target/export) throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --max-time parsing
// ---------------------------------------------------------------------------

TEST_CASE("test_max_time_parsing") {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "30"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.max_time.has_value() && *config.max_time == 30, "max_time should be 30 when provided");
        TEST_PASS("--max-time 30");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "0"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.max_time.has_value() && *config.max_time == 0, "max_time should be 0 (unlimited)");
        TEST_PASS("--max-time 0");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        expect(!config.max_time.has_value(), "max_time should be unset when --max-time omitted");
        TEST_PASS("--max-time omitted (SearchConfig keeps 180s default)");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "-1"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Negative --max-time should throw");
        TEST_PASS("--max-time negative throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "abc"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Non-numeric --max-time should throw");
        TEST_PASS("--max-time non-numeric throws");
    }
}

// ---------------------------------------------------------------------------
// Test: CLI option → SolveRequest.search_config wiring
// ---------------------------------------------------------------------------

TEST_CASE("test_solve_request_config_wiring") {
    BesqContext ctx;
    ctx.load_builtin();

    // --max-time 0 → unlimited (max_search_time == 0 ms)
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2", "--max-time", "0"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.max_search_time.count() == 0, "--max-time 0 should set max_search_time to 0 (unlimited)");
        TEST_PASS("wiring: --max-time 0 = unlimited");
    }
    // --max-time omitted → SearchConfig default 180s untouched
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.max_search_time.count() == 180'000, "omitted --max-time should keep 180s default");
        TEST_PASS("wiring: omitted --max-time keeps 180s default");
    }
    // --max-time 5 → 5000 ms
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2", "--max-time", "5"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.max_search_time.count() == 5000, "--max-time 5 should set max_search_time to 5000 ms");
        TEST_PASS("wiring: --max-time 5 = 5000 ms");
    }
    // --memory 2048 → memory_mb == 2048
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2", "--memory", "2048"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        expect(config.memory_mb == 2048, "--memory 2048 parsed");
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.memory_mb == 2048, "--memory 2048 should be wired to search_config.memory_mb");
        TEST_PASS("wiring: --memory 2048");
    }
    // --memory omitted → memory_mb stays 0 (A* uses its own 2048 fallback)
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.memory_mb == 0, "omitted --memory should leave memory_mb 0");
        TEST_PASS("wiring: omitted --memory stays 0");
    }
}

// ---------------------------------------------------------------------------
// Test: --config validation
// ---------------------------------------------------------------------------

TEST_CASE("test_config_parsing") {
    // Valid configs
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config",
                              "ignore-repair-cost=true,ignore-penalty-cost=false"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(!config.config_pairs.empty(), "multi-config should be non-empty");
        TEST_PASS("--config multiple pairs");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config", "ignore-repair-cost=true"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(!config.config_pairs.empty(), "repair-cost config should be valid");
        TEST_PASS("--config ignore-repair-cost=true");
    }

    // Invalid configs
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config", ""};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Empty --config should throw");
        TEST_PASS("--config empty throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config", "unknown-key=true"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Unknown --config key should throw");
        TEST_PASS("--config unknown key throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config", "ignore-repair-cost=maybe"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Invalid --config value should throw");
        TEST_PASS("--config invalid value throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--config", "badformat"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Malformed --config should throw");
        TEST_PASS("--config malformed throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --edit parsing
// ---------------------------------------------------------------------------

TEST_CASE("test_edit_parsing") {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--edit", "ench:mod,sharpness,max_level=10"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.edit_ops.has_value(), "edit_ops should be set");
        TEST_PASS("--edit valid ench:mod");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--edit",
                              "ench:add,custom:foo,multiplier=3,max_level=5;eq:rm,diamond_sword"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.edit_ops.has_value(), "multi-edit should be set");
        TEST_PASS("--edit multiple ops");
    }
    {
        // Missing colon in operation header
        const char* argv[] = {"besq", "--target", "diamond_sword", "--edit", "badformat"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Invalid --edit format should throw");
        TEST_PASS("--edit bad format throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--edit", ""};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Empty --edit should throw");
        TEST_PASS("--edit empty throws");
    }
}

// ---------------------------------------------------------------------------
// Test: already-met solve args parse (source == target)
// ---------------------------------------------------------------------------
// Backlog #13 residual: `--target diamond_sword[sharpness=5] --source
// sharpness=5` used to error with "目标不可达" at solve time.  Parse-level this
// must be accepted (the full 0-step solve behaviour is covered by
// test_besq_solve_already_met).

TEST_CASE("test_already_met_args_parse") {
    const char* argv[] = {"besq", "--target", "diamond_sword[sharpness=5]", "--source", "sharpness=5"};
    auto config = CLIApp::parse(5, const_cast<char**>(argv));
    expect(config.target == "diamond_sword[sharpness=5]", "target should be the bracketed inline item");
    expect(config.source == "sharpness=5", "source should be sharpness=5");
    expect(config.algorithm == "dp_merge", "default algorithm should be dp_merge");
    TEST_PASS("already-met args parse");
}

// ---------------------------------------------------------------------------
// Test: --algorithm unknown name
// ---------------------------------------------------------------------------

TEST_CASE("test_algorithm_name") {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algorithm", "astar"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.algorithm == "astar", "astar algorithm name should be stored");
        TEST_PASS("--algorithm astar");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algorithm", "nonexistent"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.algorithm == "nonexistent", "unknown algorithm name should still be stored");
        TEST_PASS("--algorithm unknown name (validated later in main)");
    }
}

// ---------------------------------------------------------------------------
// Test: no args shows usage (sets help flag, doesn't throw)
// ---------------------------------------------------------------------------

TEST_CASE("test_no_args_shows_usage") {
    const char* argv[] = {"besq"};
    auto config = CLIApp::parse(1, const_cast<char**>(argv));
    expect(config.brief_usage, "no args should set brief_usage flag (not throw)");
    TEST_PASS("test_no_args_shows_usage");
}

// ---------------------------------------------------------------------------
// Test: --memory validation
// ---------------------------------------------------------------------------

TEST_CASE("test_memory_parsing") {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "auto"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.memory_mb == 0, "memory=auto should be 0");
        TEST_PASS("--memory auto");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "256"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.memory_mb == 256, "memory should be 256");
        TEST_PASS("--memory 256");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "-1"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Negative --memory should throw");
        TEST_PASS("--memory negative throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "999999999"};
        // This is > 1048576
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Too-large --memory should throw");
        TEST_PASS("--memory too large throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --profile / --profile-dir / --publish parsing
// ---------------------------------------------------------------------------

TEST_CASE("test_profile_publish_parsing") {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--profile", "modpack"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.profile.has_value() && *config.profile == "modpack", "profile should be 'modpack'");
        TEST_PASS("--profile modpack");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--profile-dir", "/tmp/p"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.profile_dir.has_value() && *config.profile_dir == "/tmp/p", "profile_dir should be '/tmp/p'");
        TEST_PASS("--profile-dir /tmp/p");
    }
    {
        const char* argv[] = {"besq", "--publish", "mypack", "--publish-version", "1.0", "--publish-tag", "stable"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        expect(config.publish.has_value() && *config.publish == "mypack", "publish should be 'mypack'");
        expect(config.publish_version.has_value() && *config.publish_version == "1.0", "publish_version should be '1.0'");
        expect(config.publish_tag.has_value() && *config.publish_tag == "stable", "publish_tag should be 'stable'");
        TEST_PASS("--publish mypack --publish-version 1.0 --publish-tag stable");
    }
}

// ---------------------------------------------------------------------------
// Test: --input alone is a valid invocation (gate exemption)
// ---------------------------------------------------------------------------

TEST_CASE("test_input_alone_valid") {
    const char* argv[] = {"besq", "--input", "some.json"};
    auto config = CLIApp::parse(3, const_cast<char**>(argv));
    expect(config.input.has_value() && *config.input == "some.json", "--input should be set");
    expect(config.target.empty(), "--input alone must not require --target");
    TEST_PASS("--input alone parses cleanly (gate exemption)");
}

// ---------------------------------------------------------------------------
// Test: --resume parsing (checkpoint resume; self-contained)
// ---------------------------------------------------------------------------

TEST_CASE("test_resume_parsing") {
    {
        const char* argv[] = {"besq", "--resume", "states/task-3.ckpt"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        expect(config.resume.has_value() && *config.resume == "states/task-3.ckpt",
               "--resume should be set");
        expect(config.target.empty(), "--resume alone must not require --target");
        TEST_PASS("--resume parses cleanly (gate exemption)");
    }
    {
        // --resume + --target is a contradiction (checkpoint is self-contained).
        const char* argv[] = {"besq", "--resume", "x.ckpt", "--target", "diamond_sword[sharpness=5]"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); },
                      "--resume with --target should throw");
        TEST_PASS("--resume + --target throws");
    }
    {
        // Empty --resume path is rejected.
        const char* argv[] = {"besq", "--resume", ""};
        expect_throws([&] { CLIApp::parse(3, const_cast<char**>(argv)); },
                      "empty --resume path should throw");
        TEST_PASS("empty --resume throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --list-profiles / --list-langs parsing (gate-exempt info flags)
// ---------------------------------------------------------------------------

TEST_CASE("test_list_flags_parsing") {
    {
        const char* argv[] = {"besq", "--list-profiles"};
        auto config = CLIApp::parse(2, const_cast<char**>(argv));
        expect(config.list_profiles, "--list-profiles should be set");
        expect(config.target.empty(), "--list-profiles alone must not require --target");
        TEST_PASS("--list-profiles parses cleanly (gate exemption)");
    }
    {
        const char* argv[] = {"besq", "--list-langs"};
        auto config = CLIApp::parse(2, const_cast<char**>(argv));
        expect(config.list_langs, "--list-langs should be set");
        expect(config.target.empty(), "--list-langs alone must not require --target");
        TEST_PASS("--list-langs parses cleanly (gate exemption)");
    }
    {
        const char* argv[] = {"besq", "--list-profiles", "--list-langs", "--verbose"};
        auto config = CLIApp::parse(4, const_cast<char**>(argv));
        expect(config.list_profiles && config.list_langs && config.verbose,
               "both flags coexist");
        TEST_PASS("--list-profiles + --list-langs coexist");
    }
}

// ---------------------------------------------------------------------------
// Test: build_solve_request — --input drives the whole inventory solve config
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_solve_request_wiring") {
    // Pin the locale so resolved i18n error text is deterministic.
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    BesqContext ctx;
    ctx.load_builtin();

    // JSON with target → target_item populated from JSON, mode=inventory,
    // default inventory algorithm (hamming).
    {
        TempInvFile f(R"({
            "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":5}] },
            "items": [ { "type": "book", "enchants": [{"id":"sharpness","level":5}] } ]
        })");
        const char* argv[] = {"besq", "--input", f.c_str()};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.mode == AlgorithmMode::inventory, "--input implies inventory mode");
        expect(req.target_item.id == NSID("minecraft:diamond_sword"), "JSON target populated");
        expect(req.target_item.enchantments.size() == 1, "JSON target enchants populated");
        auto* inv = std::get_if<InventoryPayload>(&req.payload);
        expect(inv != nullptr, "payload is an InventoryPayload");
        expect(inv && inv->extra_items.size() == 1, "one inventory item in payload");
        expect(req.algorithm == "hamming", "default inventory algorithm is hamming");
        TEST_PASS("inventory wiring: JSON target → inventory request");
    }

    // JSON without target + no --target → inventory_requires_target
    {
        TempInvFile f(R"({
            "target": { "item": "", "enchants": [] },
            "items": []
        })");
        const char* argv[] = {"besq", "--input", f.c_str()};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        bool threw = false;
        std::string msg;
        try {
            CLIApp::build_solve_request(config, ctx);
        } catch (const std::runtime_error& e) {
            threw = true;
            msg = e.what();
        }
        expect(threw, "JSON without target + no --target should throw");
        // en_US pinned above → resolved "Inventory task requires a target item ..."
        expect(msg.find("target item") != std::string::npos, "error is inventory_requires_target (resolved en_US)");
        TEST_PASS("inventory wiring: missing target throws");
    }

    // CLI --target / --algorithm override JSON values (priority CLI > JSON)
    {
        TempInvFile f(R"({
            "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":5}] },
            "items": [],
            "algorithm": "dp_merge"
        })");
        const char* argv[] = {"besq", "--input", f.c_str(), "--target", "diamond_sword[knockback=2]", "--algorithm", "bb_dp"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.algorithm == "bb_dp", "CLI explicit algorithm beats JSON algorithm");
        expect(req.target_item.enchantments.size() == 1 &&
                   req.target_item.enchantments.find(NSID("minecraft:knockback")) != req.target_item.enchantments.end(),
               "CLI --target overrides JSON target");
        TEST_PASS("inventory wiring: CLI --target/--algorithm override JSON");
    }
}

// ---------------------------------------------------------------------------
// Helper: create a temp profiles dir with a "modded_sword" profile (depends on
// vanilla, adds the mod:ember enchantment).  Returns the directory path.
// ---------------------------------------------------------------------------
static std::string make_modded_profiles_dir() {
    auto tmp = std::filesystem::temp_directory_path() / "besq_inv_profile_switch";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "modded_sword.json");
        f << R"({"name":"modded_sword","dependencies":["builtin:vanilla"],
                  "enchantments":[{"id":"mod:ember","name":"Ember","platform":"java",
                                   "max_level":3,"multiplier":2,
                                   "supported_items":["#minecraft:swords"]}]})";
    }
    return tmp.string();
}

// ---------------------------------------------------------------------------
// Test: build_solve_request — JSON "profile" activates the profile BEFORE
// cross-validation, so profile-only content resolves
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_profile_switch") {
    // Temp profiles dir: a modded profile depending on vanilla that adds a
    // custom enchantment. If the JSON "profile" does not switch the active
    // registry before cross-validation, the mod enchantment is unknown.
    std::string tmp = make_modded_profiles_dir();

    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();
    expect(!ctx.enchantments().contains(NSID("mod:ember")), "mod:ember absent while vanilla is active");

    // The task names modded_sword; mod:ember is only valid under that profile.
    TempInvFile f(R"({
        "profile": "modded_sword",
        "target": { "item": "diamond_sword", "enchants": [{"id":"mod:ember","level":2}] },
        "items": [ { "type": "book", "enchants": [{"id":"mod:ember","level":2}] } ]
    })");
    const char* argv[] = {"besq", "--input", f.c_str()};
    auto config = CLIApp::parse(3, const_cast<char**>(argv));
    auto req = CLIApp::build_solve_request(config, ctx);
    expect(ctx.active_profile() == "modded_sword", "JSON profile activated on the context");
    expect(req.target_item.enchantments.size() == 1, "profile switch lets JSON target use the mod enchantment");

    std::filesystem::remove_all(tmp);
    TEST_PASS("inventory wiring: JSON profile switches registries before cross-validation");
}

// ---------------------------------------------------------------------------
// Test: build_solve_request — an explicit CLI --profile overrides the JSON
// profile field, so the JSON profile is NOT activated (F1)
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_explicit_profile_overrides_json") {
    std::string tmp = make_modded_profiles_dir();

    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();

    // JSON names modded_sword and uses mod:ember, but the user EXPLICITLY
    // passed `--profile builtin:vanilla`.  profile_explicit must suppress the
    // JSON activation; cross-validation then runs against vanilla where
    // mod:ember is unknown → throws.
    TempInvFile f(R"({
        "profile": "modded_sword",
        "target": { "item": "diamond_sword", "enchants": [{"id":"mod:ember","level":2}] },
        "items": [ { "type": "book", "enchants": [{"id":"mod:ember","level":2}] } ]
    })");
    const char* argv[] = {"besq", "--input", f.c_str(), "--profile", "builtin:vanilla"};
    auto config = CLIApp::parse(5, const_cast<char**>(argv));
    expect(config.profile_explicit, "profile_explicit true when --profile passed");
    bool threw = false;
    std::string msg;
    try {
        CLIApp::build_solve_request(config, ctx);
    } catch (const std::runtime_error& e) {
        threw = true;
        msg = e.what();
    }
    expect(threw, "explicit --profile builtin:vanilla suppresses JSON profile activation");
    // The only error that names the enchantment here is cli.err.unknown_ench
    // (resolved per active locale), proving cross-validation ran against
    // vanilla rather than the JSON's modded_sword.
    expect(msg.find("mod:ember") != std::string::npos, "cross-validation ran against vanilla (mod:ember unknown)");
    expect(ctx.active_profile() == "builtin:vanilla", "active profile unchanged by the JSON field when --profile explicit");

    std::filesystem::remove_all(tmp);
    TEST_PASS("inventory wiring: explicit --profile overrides JSON profile");
}

// ---------------------------------------------------------------------------
// Test: --source + --input (no --target) is rejected with the inventory
// message, not the generic source_without_target (F3)
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_source_rejection") {
    BesqContext ctx;
    ctx.load_builtin();

    TempInvFile f(R"({
        "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":5}] },
        "items": []
    })");
    const char* argv[] = {"besq", "--input", f.c_str(), "--source", "sharpness=2"};
    // parse() must NOT throw source_without_target (--input present).
    auto config = CLIApp::parse(5, const_cast<char**>(argv));
    expect(config.input.has_value(), "--input parsed");
    expect(config.source == "sharpness=2", "--source parsed (not rejected at parse)");

    bool threw = false;
    std::string msg;
    try {
        CLIApp::build_solve_request(config, ctx);
    } catch (const std::runtime_error& e) {
        threw = true;
        msg = e.what();
    }
    expect(threw, "inventory + --source should throw in build_solve_request");
    expect(msg.find("cli.err.inventory_rejects_source") != std::string::npos || msg.find("--source") != std::string::npos,
           "error is inventory_rejects_source");
    TEST_PASS("inventory wiring: --source rejected with inventory message");
}

// ---------------------------------------------------------------------------
// Test: --input with an invalid --mode throws invalid_mode (F4)
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_invalid_mode_throws") {
    const char* argv[] = {"besq", "--input", "inv.json", "--mode", "banana"};
    expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "--input with invalid --mode should throw invalid_mode");
    TEST_PASS("inventory wiring: --input --mode banana throws invalid_mode");
}

// ---------------------------------------------------------------------------
// Test: --CLIApp::apply_config_pairs functional test
// ---------------------------------------------------------------------------

TEST_CASE("test_apply_config_pairs") {
    algorithm::ForgeConfig cfg;
    cfg.ignore_penalty_cost = false;
    cfg.ignore_repair_cost = false;

    expect(!cfg.ignore_penalty_cost, "penalty cost should remain false");
    expect(!cfg.ignore_repair_cost, "repair cost should remain false");

    CLIApp::apply_config_pairs("ignore-penalty-cost=true,ignore-repair-cost=true", cfg);
    expect(cfg.ignore_penalty_cost, "penalty cost should now be true");
    expect(cfg.ignore_repair_cost, "repair cost should now be true");

    TEST_PASS("CLIApp::apply_config_pairs functional");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test: book/enchanted_book targets — parsed without an equipment-registry
// lookup and normalised to the enchanted_book that actually carries the
// enchantments (a plain book becomes an enchanted_book when enchanted).
// ---------------------------------------------------------------------------

TEST_CASE("test_book_target_parsing") {
    BesqContext ctx;
    ctx.load_builtin();

    // enchanted_book directly
    {
        const char* argv[] = {"besq", "--target", "enchanted_book[sharpness=5]"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.target_item.is_book(), "enchanted_book target flagged as book");
        expect(req.target_item.id.str() == "minecraft:enchanted_book", "enchanted_book id preserved");
        expect(req.target_item.enchantments.size() == 1, "one enchantment");
        expect(req.target_item.durability == 0, "books have no durability");
        TEST_PASS("book target: enchanted_book parses");
    }
    // plain book normalises to enchanted_book
    {
        const char* argv[] = {"besq", "--target", "book[sharpness=5]"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.target_item.id.str() == "minecraft:enchanted_book", "book normalises to enchanted_book");
        expect(req.target_item.is_book(), "book flagged as book");
        TEST_PASS("book target: book normalises to enchanted_book");
    }
    // namespaced form
    {
        const char* argv[] = {"besq", "--target", "minecraft:enchanted_book[knockback=2]"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.target_item.is_book(), "namespaced enchanted_book flagged as book");
        expect(req.target_item.enchantments.size() == 1, "one enchantment");
        TEST_PASS("book target: namespaced enchanted_book parses");
    }
}

// ---------------------------------------------------------------------------
// Test: --algo-opt → SearchConfig::extra (strategy-specific knob escape hatch)
// ---------------------------------------------------------------------------

TEST_CASE("test_algo_opt_wiring") {
    BesqContext ctx;
    ctx.load_builtin();

    // Valid: comma-separated key=value pairs land in search_config.extra
    {
        const char* argv[] = {"besq",
                              "--target",
                              "diamond_sword",
                              "--source",
                              "sharpness=2",
                              "--algo-opt",
                              "bb_dp.chunk_bits=12,idastar.threshold=1.5"};
        auto config = CLIApp::parse(7, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.extra.size() == 2, "two algo-opt pairs wired");
        expect(req.search_config.extra.at("bb_dp.chunk_bits") == "12", "bb_dp.chunk_bits value");
        expect(req.search_config.extra.at("idastar.threshold") == "1.5", "idastar.threshold value");
        TEST_PASS("--algo-opt pairs wired to extra");
    }
    // Omitted → extra stays empty
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=2"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        auto req = CLIApp::build_solve_request(config, ctx);
        expect(req.search_config.extra.empty(), "omitted --algo-opt leaves extra empty");
        TEST_PASS("--algo-opt omitted → extra empty");
    }
    // Empty value → parse throws
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algo-opt", ""};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Empty --algo-opt should throw");
        TEST_PASS("--algo-opt empty throws");
    }
    // Malformed pair (no '=' / empty key / empty value) → parse throws
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algo-opt", "bb_dp.chunk_bits"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Malformed --algo-opt should throw");
        TEST_PASS("--algo-opt malformed throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algo-opt", "=8"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Empty --algo-opt key should throw");
        TEST_PASS("--algo-opt empty key throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algo-opt", "bb_dp.chunk_bits="};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); }, "Empty --algo-opt value should throw");
        TEST_PASS("--algo-opt empty value throws");
    }
    // apply_algo_opts functional (mirror of apply_config_pairs test)
    {
        algorithm::SearchConfig cfg;
        CLIApp::apply_algo_opts("a.b=1,c=hello", cfg);
        expect(cfg.extra.size() == 2, "apply_algo_opts fills extra");
        expect(cfg.extra.at("a.b") == "1", "apply_algo_opts a.b");
        expect(cfg.extra.at("c") == "hello", "apply_algo_opts c");
        TEST_PASS("CLIApp::apply_algo_opts functional");
    }
}

// ---------------------------------------------------------------------------
// Test: CLIApp::apply_lang — env / flag precedence and invalid fallback
// ---------------------------------------------------------------------------

TEST_CASE("test_apply_lang") {
    register_builtin_translations(LanguageManager::instance());
    auto& lang_mgr = LanguageManager::instance();

    // --lang en_US
    {
        const char* argv[] = {"besq", "--lang", "en_US"};
        CLIApp::apply_lang(3, const_cast<char**>(argv));
        expect_eq(lang_mgr.active().name(), std::string("en_US"), "apply_lang: --lang en_US");
    }
    // --lang zh_CN
    {
        const char* argv[] = {"besq", "--lang", "zh_CN"};
        CLIApp::apply_lang(3, const_cast<char**>(argv));
        expect_eq(lang_mgr.active().name(), std::string("zh_CN"), "apply_lang: --lang zh_CN");
    }
    // BESQ_LANG env (no --lang)
    {
        set_env("BESQ_LANG", "zh_CN");
        const char* argv[] = {"besq"};
        CLIApp::apply_lang(1, const_cast<char**>(argv));
        expect_eq(lang_mgr.active().name(), std::string("zh_CN"), "apply_lang: BESQ_LANG env selected");
        unset_env("BESQ_LANG");
    }
    // --lang flag overrides env
    {
        set_env("BESQ_LANG", "zh_CN");
        const char* argv[] = {"besq", "--lang", "en_US"};
        CLIApp::apply_lang(3, const_cast<char**>(argv));
        expect_eq(lang_mgr.active().name(), std::string("en_US"), "apply_lang: --lang overrides env");
        unset_env("BESQ_LANG");
    }
    // Invalid --lang prints a stderr warning and keeps the base language
    {
        set_env("BESQ_LANG", "en_US");
        const char* argv[] = {"besq", "--lang", "xx_YY"};
        CLIApp::apply_lang(3, const_cast<char**>(argv));
        expect_eq(lang_mgr.active().name(), std::string("en_US"), "apply_lang: invalid --lang keeps base");
        unset_env("BESQ_LANG");
    }

    TEST_PASS("CLIApp apply_lang");
}

// ---------------------------------------------------------------------------
// Test: CLIApp::detect_target
// ---------------------------------------------------------------------------

TEST_CASE("test_detect_target") {
    {
        const char* argv[] = {"besq"};
        expect_eq(CLIApp::detect_target(1, const_cast<char**>(argv)), std::string("cli"), "detect_target: default is cli");
    }
    {
        const char* argv[] = {"besq", "--api", "gui"};
        expect_eq(CLIApp::detect_target(3, const_cast<char**>(argv)), std::string("gui"), "detect_target: --api gui");
    }
    {
        const char* argv[] = {"besq", "--api", "gui", "--target", "diamond_sword"};
        expect_eq(CLIApp::detect_target(5, const_cast<char**>(argv)), std::string("gui"),
                  "detect_target: --api found anywhere");
    }
    TEST_PASS("CLIApp detect_target");
}

// ---------------------------------------------------------------------------
// Test: CLIApp::help_text — grouped option help
// ---------------------------------------------------------------------------

TEST_CASE("test_help_text") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    auto text = CLIApp::help_text();
    expect(text.find("Usage:") != std::string::npos, "help_text has Usage header");
    expect(text.find("--target") != std::string::npos, "help_text lists --target");
    expect(text.find("--export") != std::string::npos, "help_text lists --export");
    expect(text.find("--list-algorithms") != std::string::npos, "help_text lists --list-algorithms");
    expect(text.find("--profile") != std::string::npos, "help_text lists --profile");

    TEST_PASS("CLIApp help_text");
}
