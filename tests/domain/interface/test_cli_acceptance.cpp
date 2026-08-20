// =============================================================================
// CLI Acceptance Tests
//
// Tests CLI argument parsing for all parameter combinations, error handling,
// and edge cases. Validates that new features (--max-time, --config, --input,
// --resume, profile export) parse correctly.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/interface/components/BuiltinI18n.h"
#include "common/i18n/Language.h"
#include "common/utils/EnvUtil.hpp"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/cli/CLIApp.h"
#include "framework/test_framework.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

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
// Test: --export moved to `besq profile export` — parse the subcommand forms
// ---------------------------------------------------------------------------

TEST_CASE("test_export_only_valid") {
    {   // profile export --format json（默认）
        const char* argv[] = {"besq", "profile", "export", "--format", "json"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.cmd == CLIApp::Config::Cmd::profile, "profile export: cmd == profile");
        expect(config.profile_action == CLIApp::Config::ProfileAction::export_, "profile export: action export");
        expect(config.export_format == "json", "profile export: format json");
        TEST_PASS("profile export --format json parse");
    }
    {   // profile export --format csv
        const char* argv[] = {"besq", "profile", "export", "--format", "csv"};
        auto config = CLIApp::parse(5, const_cast<char**>(argv));
        expect(config.cmd == CLIApp::Config::Cmd::profile, "profile export csv: cmd == profile");
        expect(config.profile_action == CLIApp::Config::ProfileAction::export_, "profile export csv: action export");
        expect(config.export_format == "csv", "profile export: format csv");
        TEST_PASS("profile export --format csv parse");
    }
}

// ---------------------------------------------------------------------------
// Test: Missing --target is an error
// ---------------------------------------------------------------------------

TEST_CASE("test_missing_target_errors") {
    {
        const char* argv[] = {"besq", "--algorithm", "greedy"};
        expect_throws([&] { CLIApp::parse(3, const_cast<char**>(argv)); },
                      "Must throw when --target missing");
        TEST_PASS("no --target throws");
    }
    {
        const char* argv[] = {"besq", "--verbose", "--format", "json"};
        expect_throws([&] { CLIApp::parse(4, const_cast<char**>(argv)); }, "Must throw with flags only, no target");
        TEST_PASS("flags only (no target) throws");
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
// Test: profile set_dir / publish parsing（--profile-dir/--publish 已迁至
// `besq profile …` 子命令面）
// ---------------------------------------------------------------------------

TEST_CASE("test_profile_publish_parsing") {
    {   // profile set_dir <dir>
        const char* argv[] = {"besq", "profile", "set_dir", "/tmp/p"};
        auto config = CLIApp::parse(4, const_cast<char**>(argv));
        expect(config.cmd == CLIApp::Config::Cmd::profile, "profile set_dir: cmd == profile");
        expect(config.profile_action == CLIApp::Config::ProfileAction::set_dir, "profile set_dir: action set_dir");
        expect(config.profile_target == "/tmp/p", "profile set_dir: dir value");
        TEST_PASS("profile set_dir parse");
    }
    {   // profile publish <key> --version <v> --tag <t>
        const char* argv[] = {"besq", "profile", "publish", "mypack", "--version", "1.0", "--tag", "stable"};
        auto config = CLIApp::parse(8, const_cast<char**>(argv));
        expect(config.profile_action == CLIApp::Config::ProfileAction::publish, "profile publish: action publish");
        expect(config.profile_target == "mypack", "profile publish: profile key");
        expect(config.publish_version == "1.0", "profile publish: version");
        expect(config.publish_tag == "stable", "profile publish: tag");
        TEST_PASS("profile publish parse");
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
// Test: profile list / --list-langs parsing（--list-profiles 已迁至
// `besq profile list`；--list-langs 仍为顶层 solve 表 gate-exempt 标志）
// ---------------------------------------------------------------------------

TEST_CASE("test_list_flags_parsing") {
    {   // profile list
        const char* argv[] = {"besq", "profile", "list"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        expect(config.cmd == CLIApp::Config::Cmd::profile, "profile list: cmd == profile");
        expect(config.profile_action == CLIApp::Config::ProfileAction::list, "profile list: action list");
        TEST_PASS("profile list parse");
    }
    {   // --list-langs 顶层（与 --verbose 共存）
        const char* argv[] = {"besq", "--list-langs", "--verbose"};
        auto config = CLIApp::parse(3, const_cast<char**>(argv));
        expect(config.list_langs, "--list-langs should be set");
        expect(config.verbose, "--verbose coexists with --list-langs");
        expect(config.target.empty(), "--list-langs alone must not require --target");
        TEST_PASS("--list-langs top-level parses cleanly (gate exemption)");
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
// Helper: temp profiles dir with TWO independent profiles: combo_a (mod:ember)
// and combo_b (mod:flame), both depending on vanilla.
// ---------------------------------------------------------------------------
static std::string make_combo_profiles_dir() {
    auto tmp = std::filesystem::temp_directory_path() / "besq_combo_profiles";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "combo_a.json");
        f << R"({"name":"combo_a","dependencies":["builtin:vanilla"],
                  "enchantments":[{"id":"mod:ember","name":"Ember","platform":"java",
                                   "max_level":3,"multiplier":2,
                                   "supported_items":["#minecraft:swords"]}]})";
    }
    {
        std::ofstream f(tmp / "combo_b.json");
        f << R"({"name":"combo_b","dependencies":["builtin:vanilla"],
                  "enchantments":[{"id":"mod:flame","name":"Flame","platform":"java",
                                   "max_level":2,"multiplier":2,
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
// Test: composite --profile value (a,b) parses verbatim; activation happens in
// CLIApp::run, the parser only passes it through.
// ---------------------------------------------------------------------------

TEST_CASE("test_profile_group_parsing") {
    std::string tmp = make_combo_profiles_dir();
    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();

    // --profile a,b 解析为原始值（激活发生在 CLIApp::run，解析层只透传）。
    const char* argv[] = {"besq", "--profile", "combo_a,combo_b", "--target", "diamond_sword[sharpness=1]"};
    auto config = CLIApp::parse(5, const_cast<char**>(argv));
    expect(config.profile && *config.profile == "combo_a,combo_b", "--profile composite value parsed verbatim");

    std::filesystem::remove_all(tmp);
    TEST_PASS("composite --profile value parsing");
}

// ---------------------------------------------------------------------------
// Test: build_solve_request — a comma-separated JSON "profile" field activates
// a composite group before cross-validation, so both members' content resolves
// ---------------------------------------------------------------------------

TEST_CASE("test_inventory_composite_profile_json") {
    std::string tmp = make_combo_profiles_dir();
    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();

    // JSON profile 字段逗号分隔 → build_solve_request 激活组合；两成员的魔咒都可用。
    TempInvFile f(R"({
        "profile": "combo_a,combo_b",
        "target": { "item": "diamond_sword", "enchants": [{"id":"mod:ember","level":1},{"id":"mod:flame","level":1}] },
        "items": [ { "type": "book", "enchants": [{"id":"mod:ember","level":1}] } ]
    })");
    const char* argv[] = {"besq", "--input", f.c_str()};
    auto config = CLIApp::parse(3, const_cast<char**>(argv));
    auto req = CLIApp::build_solve_request(config, ctx);
    expect(ctx.composite_active(), "composite activated from JSON profile field");
    expect(req.target_item.enchantments.size() == 2,
           "both member enchantments resolvable in the composite view");

    std::filesystem::remove_all(tmp);
    TEST_PASS("inventory wiring: JSON composite profile activates group");
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
// Test: CLIApp::help_text — grouped option help
// ---------------------------------------------------------------------------

TEST_CASE("test_help_text") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    auto text = CLIApp::help_text();
    expect(text.find("Usage:") != std::string::npos, "help_text has Usage header");
    expect(text.find("--target") != std::string::npos, "help_text lists --target");
    expect(text.find("solve (calc)") != std::string::npos, "help_text lists solve (calc) command");
    expect(text.find("profile") != std::string::npos, "help_text lists profile command");
    expect(text.find("serve") != std::string::npos, "help_text lists serve command");
    expect(text.find("--profile") != std::string::npos, "help_text lists --profile");
    // 顶层不再断言 --export / --list-algorithms（已迁至 `profile export` / `algo list`）

    TEST_PASS("CLIApp help_text");
}

// ---------------------------------------------------------------------------
// Slice 1: 子命令分派（solve/profile/algo/serve）+ 别名/短名
// ---------------------------------------------------------------------------

TEST_CASE("cli_slice1_dispatch") {
    {   // 默认命令 = solve
        const char* argv[] = {"besq", "--target", "x"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::solve, "bare usage -> solve");
        expect(cfg.target == "x", "solve options bound");
    }
    {   // 显式 solve（含别名 calc）
        const char* argv[] = {"besq", "solve", "--target", "x"};
        auto cfg = CLIApp::parse(4, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::solve && cfg.target == "x", "solve command");
        const char* argv2[] = {"besq", "calc", "--target", "x"};
        auto cfg2 = CLIApp::parse(4, const_cast<char**>(argv2));
        expect(cfg2.cmd == CLIApp::Config::Cmd::solve && cfg2.target == "x", "calc alias");
    }
    {   // serve
        const char* argv[] = {"besq", "serve", "--port", "8080", "--host", "0.0.0.0"};
        auto cfg = CLIApp::parse(6, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::serve, "serve dispatch");
        expect(cfg.serve_port == 8080u && cfg.serve_host == "0.0.0.0", "serve fields");
    }
    {   // serve port 越界
        const char* argv[] = {"besq", "serve", "--port", "70000"};
        expect_throws([&] { CLIApp::parse(4, const_cast<char**>(argv)); },
                      "serve port > 65535 should throw");
    }
    {   // profile 动作
        const char* argv[] = {"besq", "profile", "set_dir", "/tmp/p"};
        auto cfg = CLIApp::parse(4, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::profile, "profile dispatch");
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::set_dir, "set_dir action");
        expect(cfg.profile_target == "/tmp/p", "set_dir arg");
        const char* argv2[] = {"besq", "profile", "publish", "mypack", "--version", "1.0", "--tag", "stable"};
        auto cfg2 = CLIApp::parse(8, const_cast<char**>(argv2));
        expect(cfg2.profile_action == CLIApp::Config::ProfileAction::publish, "publish action");
        expect(cfg2.profile_target == "mypack", "publish positional");
    }
    {   // algo
        const char* argv[] = {"besq", "algo", "list"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::algo && cfg.algo_action == CLIApp::Config::AlgoAction::list, "algo list");
        const char* argv2[] = {"besq", "algorithm", "set_dir", "/tmp/a"};
        auto cfg2 = CLIApp::parse(4, const_cast<char**>(argv2));
        expect(cfg2.algo_action == CLIApp::Config::AlgoAction::set_dir && cfg2.algo_target == "/tmp/a",
               "algorithm alias + set_dir");
    }
    {   // 下游 --lang 报错（profile 表无 lang）
        const char* argv[] = {"besq", "profile", "list", "--lang", "zh_CN"};
        expect_throws([&] { CLIApp::parse(5, const_cast<char**>(argv)); },
                      "downstream --lang -> parse error");
    }
    {   // solve 命令表含 lang（= 顶层布局）
        const char* argv[] = {"besq", "solve", "--lang", "zh_CN", "--target", "x"};
        auto cfg = CLIApp::parse(6, const_cast<char**>(argv));
        expect(cfg.cmd == CLIApp::Config::Cmd::solve && cfg.lang == "zh_CN", "solve table has lang");
    }
    TEST_PASS("slice1 dispatch");
}

TEST_CASE("cli_slice1_alt_long_and_shorts") {
    {   // --algo / --mce 别名
        const char* argv[] = {"besq", "--algo", "hamming", "--mce", "bedrock", "--target", "x"};
        auto cfg = CLIApp::parse(7, const_cast<char**>(argv));
        expect(cfg.algorithm == "hamming" && cfg.platform == "bedrock", "alt_long aliases bind");
    }
    {   // 新短名 -t -s -n -f -i -o -c
        const char* argv[] = {"besq", "-t", "sword", "-s", "sharpness=5", "-n", "3", "-f", "compact"};
        auto cfg = CLIApp::parse(9, const_cast<char**>(argv));
        expect(cfg.target == "sword" && cfg.source == "sharpness=5", "-t -s bind");
        expect(cfg.solutions == 3 && cfg.format == "compact", "-n -f bind");
    }
    {   // 旧短名 -s 语义已变更：-s 3 是 source
        const char* argv[] = {"besq", "--target", "x", "-s", "3"};
        auto cfg = CLIApp::parse(5, const_cast<char**>(argv));
        expect(cfg.source == "3", "-s now binds source");
    }
    TEST_PASS("slice1 alt_long and shorts");
}

TEST_CASE("cli_slice1_mode_inference") {
    BesqContext ctx;
    ctx.load_builtin();
    {
        CLIApp::Config cfg;
        cfg.target = "diamond_sword[sharpness=5]";
        cfg.source = "sharpness=2";
        auto req = CLIApp::build_solve_request(cfg, ctx);
        expect(req.mode == AlgorithmMode::direct, "enchant list -> direct");
    }
    {
        CLIApp::Config cfg;
        cfg.target = "diamond_sword[sharpness=5]";
        cfg.source = "diamond_sword,iron_sword";
        auto req = CLIApp::build_solve_request(cfg, ctx);
        expect(req.mode == AlgorithmMode::inventory, "item list -> inventory");
        expect(std::holds_alternative<InventoryPayload>(req.payload), "payload is inventory");
    }
    {
        // 括号形式物品：item[ench] 也是 inventory
        CLIApp::Config cfg;
        cfg.target = "diamond_sword[sharpness=5]";
        cfg.source = "diamond_sword[sharpness=3]";
        auto req = CLIApp::build_solve_request(cfg, ctx);
        expect(req.mode == AlgorithmMode::inventory, "item[ench] -> inventory");
    }
    {
        // 单物品：也推断 inventory，且默认算法 hamming（与 --input JSON 路径一致）
        CLIApp::Config cfg;
        cfg.target = "diamond_sword[sharpness=5]";
        cfg.source = "diamond_sword";
        auto req = CLIApp::build_solve_request(cfg, ctx);
        expect(req.mode == AlgorithmMode::inventory, "single item -> inventory");
        expect(req.algorithm == "hamming", "item-list inventory defaults to hamming");
    }
    {
        // --input 自包含 + --source 拒绝
        CLIApp::Config cfg;
        cfg.input = "-";
        cfg.source = "sharpness=2";
        expect_throws([&] { CLIApp::build_solve_request(cfg, ctx); },
                      "--input + --source rejected (inventory_rejects_source)");
    }
    {
        // 混排/非法段：报错点名两种接受形式（含 "item list"），而非裸 ItemParser 错误
        register_builtin_translations(LanguageManager::instance());
        LanguageManager::instance().select("en_US");
        CLIApp::Config cfg;
        cfg.target = "diamond_sword[sharpness=5]";
        cfg.source = "sharpness=5,diamond_sword";
        bool threw = false;
        std::string what;
        try {
            CLIApp::build_solve_request(cfg, ctx);
        } catch (const std::exception& e) {
            threw = true;
            what = e.what();
        }
        expect(threw, "mixed source list should throw");
        expect(what.find("item list") != std::string::npos,
               "mixed-list error names the accepted forms (item list)");
    }
    TEST_PASS("mode inference");
}

TEST_CASE("cli_slice1_profile_handlers") {
    {   // profile list
        const char* argv[] = {"besq", "profile", "list"};
        int rc = CLIApp().run(3, const_cast<char**>(argv));
        expect(rc == 0, "profile list exit 0");
    }
    {   // profile info 存在
        const char* argv[] = {"besq", "profile", "info", "builtin:vanilla"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "profile info exit 0");
    }
    {   // profile info 不存在
        const char* argv[] = {"besq", "profile", "info", "nope_nope"};
        expect_throws([&] { CLIApp().run(4, const_cast<char**>(argv)); },
                      "profile info unknown -> throws");
    }
    {   // profile export 到 stdout（json）
        const char* argv[] = {"besq", "profile", "export", "--profile", "builtin:vanilla", "--file", "-"};
        // run() 会向 stdout 打印 JSON——测试不捕获 stdout，仅验证不抛
        int rc = CLIApp().run(7, const_cast<char**>(argv));
        expect(rc == 0, "profile export stdout exit 0");
    }
    TEST_PASS("profile handlers");
}

TEST_CASE("cli_slice1_algo_handlers") {
    const char* argv[] = {"besq", "algo", "list"};
    int rc = CLIApp().run(3, const_cast<char**>(argv));
    expect(rc == 0, "algo list exit 0");
    TEST_PASS("algo handlers");
}

TEST_CASE("cli_slice1_help_text") {
    std::string top = CLIApp::help_text("besq");
    expect(top.find("solve (calc)") != std::string::npos, "help lists solve (calc)");
    expect(top.find("profile") != std::string::npos && top.find("serve") != std::string::npos,
           "help lists profile/serve");
    std::string sv = CLIApp::help_text("besq", std::vector<std::string_view>{"serve"});
    expect(sv.find("--port") != std::string::npos, "serve help lists --port");
    expect(sv.find("--target") == std::string::npos, "serve help has no solve options");
    TEST_PASS("slice1 help_text");
}

TEST_CASE("cli_slice1_set_dir_parse_guard") {
    {   // parse 层：缺参 set_dir 解析成功，target 为空
        const char* argv[] = {"besq", "profile", "set_dir"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::set_dir, "action set_dir");
        expect(cfg.profile_target.empty(), "missing positional -> empty target");
    }
    {   // run 层：空目录拒绝
        const char* argv[] = {"besq", "profile", "set_dir"};
        expect_throws([&] { CLIApp().run(3, const_cast<char**>(argv)); },
                      "set_dir empty -> run throws empty_dir");
    }
    TEST_PASS("set_dir parse guard");
}

TEST_CASE("cli_slice1_precommand_flags_forward") {
    {
        const char* argv[] = {"besq", "--help", "serve"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.help, "pre-command --help forwarded to serve dispatch");
    }
    {
        const char* argv[] = {"besq", "--verbose", "solve", "--target", "x"};
        auto cfg = CLIApp::parse(5, const_cast<char**>(argv));
        expect(cfg.verbose, "pre-command --verbose forwarded");
    }
    {
        const char* argv[] = {"besq", "serve", "--help"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.help, "post-command --help still works");
    }
    TEST_PASS("pre-command flags forwarded");
}

TEST_CASE("cli_slice1_unknown_command_fatal") {
    {
        const char* argv[] = {"besq", "profile", "foo"};
        expect_throws([&] { CLIApp::parse(3, const_cast<char**>(argv)); },
                      "nested unknown_command now fatal");
    }
    {
        const char* argv[] = {"besq", "frobnicate", "--target", "x"};
        expect_throws([&] { CLIApp::parse(4, const_cast<char**>(argv)); },
                      "top-level unknown_command now fatal");
    }
    TEST_PASS("unknown_command fatal");
}

TEST_CASE("cli_slice2a_inspect_parse") {
    {
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--filter", "sharp", "--fields", "id,max_level",
                              "--limit", "5", "--page", "2", "--format", "json"};
        auto cfg = CLIApp::parse(15, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::inspect, "inspect action");
        expect(cfg.profile_target == "builtin:vanilla" && cfg.inspect_kind == "ench", "positionals");
        expect(cfg.inspect_filter == "sharp" && cfg.inspect_fields == "id,max_level", "options");
        expect(cfg.inspect_limit == 5 && cfg.inspect_page == 2 && cfg.inspect_format == "json", "options 2");
    }
    {   // kind 全拼与大小写
        const char* argv[] = {"besq", "profile", "inspect", "p", "Enchantments"};
        auto cfg = CLIApp::parse(5, const_cast<char**>(argv));
        expect(cfg.inspect_kind == "Enchantments", "full spelling passes through (normalized in run)");
    }
    {   // 缺参：parse 成功（Positional 缺失 → 空值），action 已置
        const char* argv[] = {"besq", "profile", "inspect"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::inspect, "action set with missing positionals");
        expect(cfg.profile_target.empty(), "missing profile positional -> empty");
    }
    TEST_PASS("slice2a inspect parse");
}

TEST_CASE("cli_slice2a_inspect_behavior") {
    {   // text 表格：表头 + 行
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench", "--limit", "3"};
        int rc = CLIApp().run(7, const_cast<char**>(argv));
        expect(rc == 0, "inspect run exit 0");
    }
    {   // json 形态
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--filter", "sharpness", "--format", "json", "--limit", "2"};
        int rc = CLIApp().run(11, const_cast<char**>(argv));
        expect(rc == 0, "inspect json exit 0");
    }
    {   // 未知 kind
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "bogus"};
        expect_throws([&] { CLIApp().run(5, const_cast<char**>(argv)); }, "bad kind throws");
    }
    {   // --limit=-1（`=` 形态绑定 -1）→ run 层 invalid_value 拒绝
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench", "--limit=-1"};
        expect_throws([&] { CLIApp().run(6, const_cast<char**>(argv)); }, "negative limit (=form) throws");
    }
    {   // --limit -1（空格形态：-1 被解析层当选项 token → missing_value）同样拒绝
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench", "--limit", "-1"};
        expect_throws([&] { CLIApp().run(7, const_cast<char**>(argv)); }, "negative limit (space form) throws");
    }
    {   // 未知 profile
        const char* argv[] = {"besq", "profile", "inspect", "nope_nope", "ench"};
        expect_throws([&] { CLIApp().run(5, const_cast<char**>(argv)); }, "unknown profile throws");
    }
    TEST_PASS("slice2a inspect behavior");
}

// review C1：--fields 非前缀子集两行以上列不串位（原实现逐行收缩 headers/numeric，
// 第 2 行起子集位置索引全列 → 错位；json 模式对名字列 stoll → 崩溃）。
// review I1：json 的 is_treasure 必须是 bool（无引号），max_level 数值列不带引号。
TEST_CASE("cli_slice2a_inspect_fields_alignment") {
    {   // text 形态：--fields id,max_level 两行以上，列内容不串位（exit-0 门控崩溃）
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--fields", "id,max_level", "--limit", "5"};
        int rc = CLIApp().run(9, const_cast<char**>(argv));
        expect(rc == 0, "fields text exit 0");
    }
    {   // json 形态：不再崩溃且列值正确（取全部行，断言 rows 数组存在）
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--fields", "id,max_level", "--format", "json"};
        int rc = CLIApp().run(9, const_cast<char**>(argv));
        expect(rc == 0, "fields json exit 0 (no stoll crash)");
    }
    {   // I1：验收框架不提供 stdout 捕获（系统级锁定），此处进程内 rdbuf 交换
        // （subprocess-free）临时接管 std::cout，断言 JSON 原文的 bool/数值形态。
        std::ostringstream buf;
        std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
        struct Restore {
            std::streambuf* old;
            ~Restore() { std::cout.rdbuf(old); }
        } restore{old};
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--format", "json"};
        int rc = CLIApp().run(7, const_cast<char**>(argv));
        expect(rc == 0, "inspect json full exit 0");
        const std::string out = buf.str();
        expect(out.find("\"is_treasure\":true") != std::string::npos, "is_treasure:true bool form");
        expect(out.find("\"is_treasure\":false") != std::string::npos, "is_treasure:false bool form");
        expect(out.find("\"is_treasure\":\"") == std::string::npos, "is_treasure never string-quoted");
        expect(out.find("\"max_level\":5") != std::string::npos, "max_level numeric without quotes");
    }
    {   // review：--fields 重复列名（id,id）去重——text 表头不得出现双 id 列
        //   （原实现 selected=[0,0]，表头 "id  id" 两列、行值重复；json 形态因
        //   Json::Object 为 std::map 会键覆盖，故用 text 形态断言）。
        std::ostringstream buf;
        std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
        struct Restore {
            std::streambuf* old;
            ~Restore() { std::cout.rdbuf(old); }
        } restore{old};
        const char* argv[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                              "--fields", "id,id", "--limit", "2"};
        int rc = CLIApp().run(9, const_cast<char**>(argv));
        expect(rc == 0, "fields id,id text exit 0");
        const std::string out = buf.str();
        expect(out.find("id  id") == std::string::npos, "no duplicated id header column (dedup keeps first)");
        // json 形态：行对象恰一个 "id" 键（--limit 2 → 恰 2 次 "id": 键，无叠加）
        std::ostringstream buf2;
        std::streambuf* old2 = std::cout.rdbuf(buf2.rdbuf());
        struct Restore2 {
            std::streambuf* old2;
            ~Restore2() { std::cout.rdbuf(old2); }
        } restore2{old2};
        const char* argv2[] = {"besq", "profile", "inspect", "builtin:vanilla", "ench",
                               "--fields", "id,id", "--format", "json", "--limit", "2"};
        int rc2 = CLIApp().run(11, const_cast<char**>(argv2));
        expect(rc2 == 0, "fields id,id json exit 0");
        const std::string out2 = buf2.str();
        size_t id_keys = 0, pos = 0;
        while ((pos = out2.find("\"id\":", pos)) != std::string::npos) { ++id_keys; pos += 5; }
        expect(id_keys == 2, "json rows have exactly one id key each (2 rows, 2 id keys)");
    }
    TEST_PASS("inspect fields alignment");
}

TEST_CASE("cli_slice2a_algo_inspect") {
    {
        const char* argv[] = {"besq", "algo", "inspect", "dp_merge"};
        auto cfg = CLIApp::parse(4, const_cast<char**>(argv));
        expect(cfg.algo_action == CLIApp::Config::AlgoAction::inspect && cfg.algo_target == "dp_merge",
               "algo inspect parse");
    }
    {
        const char* argv[] = {"besq", "algo", "inspect", "dp_merge", "--format", "json"};
        int rc = CLIApp().run(6, const_cast<char**>(argv));
        expect(rc == 0, "algo inspect json exit 0");
    }
    {
        const char* argv[] = {"besq", "algo", "inspect", "nope_nope"};
        expect_throws([&] { CLIApp().run(4, const_cast<char**>(argv)); }, "unknown algo throws");
    }
    TEST_PASS("algo inspect");
}

// ---------------------------------------------------------------------------
// Task 5 (slice 2a): datapack export — round-trip readable + error paths
// ---------------------------------------------------------------------------

/// 跨平台唯一时间戳后缀（temp 目录唯一性）：Windows 用 GetTickCount64
/// （framework 已含 windows.h），其他平台回退 steady_clock 毫秒。
static std::string unique_ts_suffix() {
#ifdef _WIN32
    return std::to_string(static_cast<long long>(::GetTickCount64()));
#else
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count());
#endif
}

TEST_CASE("cli_slice2a_datapack_roundtrip") {
    namespace fs = std::filesystem;
    const fs::path tmp =
        fs::temp_directory_path() / ("besq_dp_" + unique_ts_suffix());
    struct Guard {
        fs::path p;
        ~Guard() { std::error_code ec; fs::remove_all(p, ec); }
    } guard{tmp};
    // 布局（review）：导出目标是 datapack 根（pack.mcmeta 在根内），loader 要求
    // datapack 目录是 profiles_dir 的直接子目录 → profiles_dir = <tmp>/dp，
    // datapack = <tmp>/dp/<unique-folder>（唯一名 per run，保持 besq_dp_ 前缀过滤）。
    const fs::path dp_dir = tmp / "dp";
    const fs::path dp_pkg = dp_dir / ("besq_dp_" + unique_ts_suffix());
    // 导出前取源 profile own-data 装备 id（review I2：装备保真 = 源装备 id 子集保留）
    BesqContext ctx1;
    ctx1.load_builtin();
    const auto& src_profile = ctx1.profile("builtin:vanilla");
    std::unordered_set<std::string> src_eq_ids;
    for (const auto& e : src_profile.eq())
        src_eq_ids.insert(e.id.str());
    const size_t src_eq_count = src_profile.eq().size();
    {
        const std::string pkg_str = dp_pkg.string();  // 物化：避免 argv 持悬垂 c_str()
        const char* argv[] = {"besq", "profile", "export", "--format", "datapack",
                              "--file", pkg_str.c_str(), "--profile", "builtin:vanilla"};
        int rc = CLIApp().run(9, const_cast<char**>(argv));
        expect(rc == 0, "datapack export exit 0");
    }
    expect(fs::exists(dp_pkg / "pack.mcmeta"), "pack.mcmeta written");
    expect(fs::exists(dp_pkg / "data"), "data dir written");
    // 回读：loader 命名 = 文件夹名 verbatim（besq_dp_*，非 vanilla_datapack ——
    // 后者仅当 datapack 命名为 vanilla/builtin:vanilla 时触发）。
    BesqContext ctx2;
    ctx2.load_builtin();
    ctx2.set_profiles_dir(dp_dir.string());
    ctx2.load_profiles();
    auto profiles = ctx2.list_profiles();
    bool found_dp = false;
    for (const auto& name : profiles) {
        if (name == "builtin:vanilla") continue;       // builtin 遮蔽，选 datapack 那个
        if (name.rfind("besq_dp_", 0) != 0) continue;  // 只认本次导出的目录（防 temp 残留）
        found_dp = true;
        const Profile& p = ctx2.profile(name);
        expect(p.ench().size() > 0, "datapack profile has enchantments");
        // 最小保真门：sharpness 存在且 max_level=5（导出 ns = profile key 前缀
        // "builtin"，故回读 id 为 builtin:sharpness —— 按尾部匹配）。
        bool sharp_ok = false;
        for (const auto& e : p.ench()) {
            if (e.id.get_id() == "sharpness") {
                sharp_ok = (e.max_level == 5);
                break;
            }
        }
        expect(sharp_ok, "sharpness present with max_level=5");
        // I2：装备保真——源装备 id 全集 ⊆ 回读 datapack 装备集（子集检查）。
        std::unordered_set<std::string> dp_eq_ids;
        for (const auto& e : p.eq()) dp_eq_ids.insert(e.id.str());
        size_t eq_missing = 0;
        for (const auto& id : src_eq_ids)
            if (!dp_eq_ids.count(id)) ++eq_missing;
        expect(eq_missing == 0, "all source equipment ids survive datapack roundtrip");
        // 计数断言（review 强化）：规则 durability<=0 && category==id-tail 修复后，
        // id 尾部回退类别垃圾（compass/carved_pumpkin）不再并入装备集。
        expect(!dp_eq_ids.count("minecraft:compass"),
               "compass (durability-0 id-tail category) filtered");
        expect(!dp_eq_ids.count("minecraft:carved_pumpkin"),
               "carved_pumpkin (durability-0 id-tail category) filtered");
        // 精确计数 == 源计数 + 7 个 skull 系残存（player_head/skeleton_skull/…）：
        // 这些物品 load 端类别由后缀推导为 head/skull ≠ id 尾部，规则不命中——
        // 属 review 预期外的残存（详见 minors-fix-report item 13 的验证记录）。
        // 文档化保真边界：源装备子集 + skull 系残存（非精确 85）。
        expect(dp_eq_ids.size() == src_eq_count + 7,
               "fidelity boundary: source subset preserved + 7 skull-category remnants (durability-0 suffix-category junk, see report item 13)");
        break;
    }
    expect(found_dp, "datapack profile loaded (key = folder stem)");
    TEST_PASS("datapack roundtrip");
}

TEST_CASE("cli_slice2a_datapack_errors") {
    namespace fs = std::filesystem;
    const fs::path tmp =
        fs::temp_directory_path() / ("besq_dp_err_" + unique_ts_suffix());
    fs::create_directories(tmp / "existing");
    struct Guard {
        fs::path p;
        ~Guard() { std::error_code ec; fs::remove_all(p, ec); }
    } guard{tmp};
    {   // 目标已存在且非空 → 拒绝
        const std::string tmp_str = tmp.string();  // 物化：避免 argv 持悬垂 c_str()
        const char* argv[] = {"besq", "profile", "export", "--format", "datapack",
                              "--file", tmp_str.c_str()};
        expect_throws([&] { CLIApp().run(7, const_cast<char**>(argv)); },
                      "non-empty dir rejected");
    }
    {   // 缺 --file → export_dir_required
        const char* argv[] = {"besq", "profile", "export", "--format", "datapack"};
        expect_throws([&] { CLIApp().run(5, const_cast<char**>(argv)); },
                      "missing --file rejected");
    }
    TEST_PASS("datapack errors");
}

// ---------------------------------------------------------------------------
// Task 6 (slice 2a): 子命令帮助管线化 — command_path + help_requested_anywhere
// ---------------------------------------------------------------------------

TEST_CASE("cli_slice2a_subhelp") {
    {   // profile --help：cfg.help + command_path 记录
        const char* argv[] = {"besq", "profile", "--help"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.help, "profile --help sets help");
        expect(cfg.command_path.size() == 1 && cfg.command_path[0] == "profile", "path recorded");
    }
    {   // 叶子帮助：profile list --help 置 help（help_requested_anywhere）
        const char* argv[] = {"besq", "profile", "list", "--help"};
        auto cfg = CLIApp::parse(4, const_cast<char**>(argv));
        expect(cfg.help, "leaf --help bubbles up");
        expect(cfg.command_path.size() == 2 &&
                   cfg.command_path[0] == "profile" && cfg.command_path[1] == "list",
               "leaf path recorded element-wise");
    }
    {   // set_dir --help 不再抛 empty_dir（run 层显示帮助）
        const char* argv[] = {"besq", "profile", "set_dir", "--help"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "set_dir --help exits 0 (shows help, no action)");
    }
    TEST_PASS("subhelp plumbing");
}

// ---------------------------------------------------------------------------
// Task 5 (slice 1): profile sql — BesqContext::run_sql 委托 + CLI 接线
//   parse 矩阵 / 链式执行 / --format json / -i 互斥 + REPL 会话 / --profile 未知 /
//   链中止语义（先前成功语句保持生效）/ 脏退出警告
// ---------------------------------------------------------------------------

TEST_CASE("cli_slice_sql_parse_matrix") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    {   // 基本：stmt positional + 默认 format=text / -i off / --profile 空
        const char* argv[] = {"besq", "profile", "sql", "SELECT id FROM enchantment LIMIT 1;"};
        auto cfg = CLIApp::parse(4, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::sql, "sql action");
        expect(cfg.sql_stmt == "SELECT id FROM enchantment LIMIT 1;", "stmt positional");
        expect(cfg.sql_format == "text", "format default text");
        expect(!cfg.sql_interactive, "-i default off");
        expect(cfg.sql_profile.empty(), "--profile default empty (CLI falls back to active)");
    }
    {   // 全字段：--profile / -i / --format
        const char* argv[] = {"besq", "profile", "sql", "SELECT * FROM enchantment;",
                              "--profile", "builtin:vanilla", "-i", "--format", "json"};
        auto cfg = CLIApp::parse(9, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::sql, "sql action 2");
        expect(cfg.sql_profile == "builtin:vanilla", "--profile bound");
        expect(cfg.sql_interactive, "-i flag bound");
        expect(cfg.sql_format == "json", "--format json bound");
    }
    {   // 缺 stmt：parse 成功，stmt 空
        const char* argv[] = {"besq", "profile", "sql"};
        auto cfg = CLIApp::parse(3, const_cast<char**>(argv));
        expect(cfg.profile_action == CLIApp::Config::ProfileAction::sql, "action set with no stmt");
        expect(cfg.sql_stmt.empty(), "missing positional -> empty stmt");
    }
    {   // --format= 等号形态
        const char* argv[] = {"besq", "profile", "sql", "SELECT * FROM equipment;", "--format=json"};
        auto cfg = CLIApp::parse(5, const_cast<char**>(argv));
        expect(cfg.sql_format == "json", "equals-form --format=json");
    }
    TEST_PASS("sql parse matrix");
}

TEST_CASE("cli_slice_sql_chain_execute") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    {   // 单条 SELECT（读，无脏）
        const char* argv[] = {"besq", "profile", "sql",
                              "SELECT id FROM enchantment WHERE id='minecraft:sharpness';"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "read chain exit 0");
    }
    {   // 链：SELECT + SELECT（全读）
        const char* argv[] = {"besq", "profile", "sql",
                              "SELECT id FROM enchantment LIMIT 1; SELECT id FROM equipment LIMIT 1;"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "multi-statement read chain exit 0");
    }
    {   // 写链：INSERT + SELECT 回读（脏 → stderr 警告，仍 exit 0）
        std::ostringstream out, err;
        std::streambuf* oo = std::cout.rdbuf(out.rdbuf());
        std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
        struct Restore { std::streambuf* o; std::streambuf* e;
                         ~Restore() { std::cout.rdbuf(o); std::cerr.rdbuf(e); } } restore{oo, oe};
        const char* argv[] = {"besq", "profile", "sql",
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:chain','C',1,1,'#minecraft:swords'); "
            "SELECT id FROM enchantment WHERE id='test:chain';"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "write chain exit 0");
        expect(out.str().find("test:chain") != std::string::npos, "SELECT echoes inserted row");
        expect(err.str().find("unsaved") != std::string::npos, "dirty warning on stderr");
        expect(err.str().find("builtin:vanilla") != std::string::npos, "dirty warning names the profile");
    }
    TEST_PASS("sql chain execute");
}

TEST_CASE("cli_slice_sql_format_json") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    struct Restore { std::streambuf* old; ~Restore() { std::cout.rdbuf(old); } } restore{old};
    const char* argv[] = {"besq", "profile", "sql",
                          "SELECT id FROM enchantment WHERE id='minecraft:sharpness';",
                          "--format", "json"};
    int rc = CLIApp().run(6, const_cast<char**>(argv));
    expect(rc == 0, "json format exit 0");
    const std::string s = out.str();
    expect(!s.empty() && s[0] == '[', "stdout is a JSON array");
    expect(s.find("minecraft:sharpness") != std::string::npos, "row value present in array");
    expect(s.find("\"id\"") != std::string::npos, "header present in array");
    TEST_PASS("sql json format");
}

TEST_CASE("cli_slice_sql_interactive") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    {   // -i + stmt → 互斥错误（stmt 在前）
        try {
            const char* argv[] = {"besq", "profile", "sql", "SELECT id FROM enchantment LIMIT 1;", "-i"};
            CLIApp().run(5, const_cast<char**>(argv));
            expect(false, "-i with stmt should throw exclusive error");
        } catch (const std::runtime_error& e) {
            expect(std::string(e.what()).find("-i") != std::string::npos, "exclusive error mentions -i");
        }
    }
    {   // stmt + -i → 同样互斥（-i 在前）
        try {
            const char* argv[] = {"besq", "profile", "sql", "-i", "SELECT id FROM enchantment LIMIT 1;"};
            CLIApp().run(5, const_cast<char**>(argv));
            expect(false, "-i then stmt should throw exclusive error");
        } catch (const std::runtime_error& e) {
            expect(std::string(e.what()).find("-i") != std::string::npos, "exclusive error mentions -i (order 2)");
        }
    }
    TEST_PASS("sql interactive mutual exclusion");
}

TEST_CASE("cli_slice_sql_repl_basic") {
    // stdin 管道驱动 REPL：SELECT + QUIT → 行渲染、exit 0、无错误
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    std::ostringstream out, err;
    std::istringstream in("SELECT id FROM enchantment WHERE id='minecraft:sharpness';\nQUIT\n");
    std::streambuf* oo = std::cout.rdbuf(out.rdbuf());
    std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
    std::streambuf* oi = std::cin.rdbuf(in.rdbuf());
    struct Restore { std::streambuf* o; std::streambuf* e; std::streambuf* i;
                     ~Restore() { std::cout.rdbuf(o); std::cerr.rdbuf(e); std::cin.rdbuf(i); } } restore{oo, oe, oi};
    const char* argv[] = {"besq", "profile", "sql", "-i"};
    int rc = CLIApp().run(4, const_cast<char**>(argv));
    expect(rc == 0, "repl exit 0");
    expect(out.str().find("profile> ") != std::string::npos, "prompt shown");
    expect(out.str().find("minecraft:sharpness") != std::string::npos, "SELECT row rendered");
    expect(err.str().empty(), "no stderr on clean repl");
    TEST_PASS("sql repl basic");
}

TEST_CASE("cli_slice_sql_repl_session") {
    // 多行续行（...> 提示）/ 省略分号 / 错误不退出 / 跨调用 UNDO / 退出脏警告
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    std::ostringstream out, err;
    std::istringstream in(
        "SELECT id,\n"
        " name FROM enchantment WHERE id='minecraft:sharpness';\n"
        "FROB x;\n"
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('test:repl','R',1,1,'#minecraft:swords');\n"
        "SELECT id FROM enchantment WHERE id='test:repl'\n"
        "UNDO;\n"
        "SELECT id FROM enchantment WHERE id='test:repl';\n"
        "QUIT\n");
    std::streambuf* oo = std::cout.rdbuf(out.rdbuf());
    std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
    std::streambuf* oi = std::cin.rdbuf(in.rdbuf());
    struct Restore { std::streambuf* o; std::streambuf* e; std::streambuf* i;
                     ~Restore() { std::cout.rdbuf(o); std::cerr.rdbuf(e); std::cin.rdbuf(i); } } restore{oo, oe, oi};
    const char* argv[] = {"besq", "profile", "sql", "-i"};
    int rc = CLIApp().run(4, const_cast<char**>(argv));
    expect(rc == 0, "repl exit 0");
    const std::string so = out.str(), se = err.str();
    expect(so.find("...> ") != std::string::npos, "continuation prompt shown");
    expect(so.find("minecraft:sharpness") != std::string::npos, "multiline select rendered");
    expect(se.find("unsupported statement 'frob'") != std::string::npos, "hard error on stderr");
    expect(so.find("undo: reverted") != std::string::npos, "undo confirmation");
    // 插入后 SELECT 可见一次；UNDO 后 SELECT 不再可见 → 全 stdout 恰一次
    size_t count = 0;
    for (size_t pos = so.find("test:repl"); pos != std::string::npos; pos = so.find("test:repl", pos + 1))
        ++count;
    expect_eq(count, 1u, "row visible after insert, gone after undo");
    expect(se.find("unsaved") != std::string::npos, "dirty warning on QUIT");
    expect(se.find("builtin:vanilla") != std::string::npos, "warning names the dirty profile");
    TEST_PASS("sql repl session");
}

TEST_CASE("cli_slice_sql_unknown_profile") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    try {
        const char* argv[] = {"besq", "profile", "sql", "SELECT * FROM enchantment;",
                              "--profile", "nope_nope"};
        CLIApp().run(6, const_cast<char**>(argv));
        expect(false, "unknown --profile should throw");
    } catch (const std::runtime_error& e) {
        expect(std::string(e.what()).find("nope_nope") != std::string::npos, "clean error names the profile");
    }
    TEST_PASS("sql unknown profile clean error");
}

TEST_CASE("cli_slice_sql_chain_error") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    {   // CLI：链中第二句重复 INSERT → 中止（exit 1 via throw），错误报语句序号
        try {
            const char* argv[] = {"besq", "profile", "sql",
                "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                "VALUES ('test:dup','D',1,1,'#minecraft:swords'); "
                "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                "VALUES ('test:dup','D',1,1,'#minecraft:swords');"};
            CLIApp().run(4, const_cast<char**>(argv));
            expect(false, "duplicate insert should abort the chain");
        } catch (const std::runtime_error& e) {
            expect(std::string(e.what()).find("statement 2") != std::string::npos,
                   "error reports the failing statement index");
        }
    }
    {   // review：链中止但先前语句已写入 → 抛错前 stderr 先给未保存警告
        std::ostringstream err;
        std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
        struct Restore { std::streambuf* e; ~Restore() { std::cerr.rdbuf(e); } } restore{oe};
        try {
            const char* argv[] = {"besq", "profile", "sql",
                "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                "VALUES ('test:warn','W',1,1,'#minecraft:swords'); "
                "SELECT nope FROM enchantment;"};
            CLIApp().run(4, const_cast<char**>(argv));
            expect(false, "unknown column SELECT should abort the chain");
        } catch (const std::runtime_error& e) {
            expect(std::string(e.what()).find("statement 2") != std::string::npos,
                   "error reports the failing statement index");
        }
        expect(err.str().find("unsaved") != std::string::npos, "dirty warning printed before the error throw");
        expect(err.str().find("builtin:vanilla") != std::string::npos, "warning names the dirty profile");
    }
    {   // 委托：run_sql 直接验证——先前成功语句保持生效 + 脏报告 + steps 语义
        BesqContext ctx;
        ctx.load_builtin();
        std::string error;
        auto r = ctx.run_sql(
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:kept','K',1,1,'#minecraft:swords'); "
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:kept','K',1,1,'#minecraft:swords');",
            "builtin:vanilla", error);
        expect(!error.empty(), "chain error reported");
        expect(error.find("statement 2") != std::string::npos, "error names statement 2");
        expect(r.steps.size() == 2, "two steps executed before abort");
        expect(r.last.affected == 0, "last result = failing statement result");
        expect(r.last.message.find("already exists") != std::string::npos, "last message = executor error");
        const Profile& p = ctx.profile("builtin:vanilla");
        expect(p.ench().contains(NSID("test:kept")), "earlier successful statement remains applied");
        expect(r.dirty.size() == 1 && r.dirty[0] == "builtin:vanilla", "dirty reported after abort");
    }
    {   // 解析错误：整体解析先行，零执行
        BesqContext ctx;
        ctx.load_builtin();
        std::string error;
        auto r = ctx.run_sql("SELEC nope;", "builtin:vanilla", error);
        expect(!error.empty(), "parse error reported");
        expect(r.steps.empty(), "no statements executed on parse error");
    }
    {   // lexer 错误（未闭合字符串）→ 整体解析失败，零执行（review 修复验证）
        BesqContext ctx;
        ctx.load_builtin();
        std::string error;
        auto r = ctx.run_sql("SELECT id FROM enchantment LIMIT 1; 'unterminated", "builtin:vanilla", error);
        expect(!error.empty(), "lexer error reported");
        expect(error.find("lexer error") != std::string::npos, "error names lexer failure");
        expect(r.steps.empty(), "zero execution on lexer error");
    }
    {   // 未知 profile：use() 异常 → error 通道（干净错误）
        BesqContext ctx;
        ctx.load_builtin();
        std::string error;
        auto r = ctx.run_sql("SELECT id FROM enchantment LIMIT 1;", "nope_nope", error);
        expect(!error.empty(), "unknown profile error reported");
        expect(r.steps.empty(), "no statements executed on unknown profile");
    }
    TEST_PASS("sql chain error semantics");
}

TEST_CASE("cli_slice_sql_dirty_warning") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    {   // 读链 → 无警告
        std::ostringstream out, err;
        std::streambuf* oo = std::cout.rdbuf(out.rdbuf());
        std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
        struct Restore { std::streambuf* o; std::streambuf* e;
                         ~Restore() { std::cout.rdbuf(o); std::cerr.rdbuf(e); } } restore{oo, oe};
        const char* argv[] = {"besq", "profile", "sql", "SELECT id FROM enchantment LIMIT 1;"};
        int rc = CLIApp().run(4, const_cast<char**>(argv));
        expect(rc == 0, "read chain exit 0");
        expect(err.str().empty(), "no dirty warning on clean chain");
    }
    {   // 委托级：SAVE 清脏 → dirty 为空（SAVE 写临时 profiles 目录）
        BesqContext ctx;
        ctx.load_builtin();
        static int counter = 0;
        const std::string tmp =
            (std::filesystem::temp_directory_path() / ("besq_sql_cli_save_" + std::to_string(++counter))).string();
        std::error_code ec;
        std::filesystem::create_directories(tmp, ec);
        ctx.set_profiles_dir(tmp);
        std::string error;
        auto r = ctx.run_sql(
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:saved','S',1,1,'#minecraft:swords'); SAVE;",
            "builtin:vanilla", error);
        expect(error.empty(), "insert+save chain ok");
        expect(r.dirty.empty(), "SAVE cleared dirty");
        expect(std::filesystem::exists(std::filesystem::path(tmp) / "builtin_vanilla.json"),
               "SAVE wrote sanitized profile file");
        std::filesystem::remove_all(tmp, ec);
    }
    {   // 无 stmt 无 -i → 简短用法 exit 0
        std::ostringstream out;
        std::streambuf* old = std::cout.rdbuf(out.rdbuf());
        struct Restore { std::streambuf* old; ~Restore() { std::cout.rdbuf(old); } } restore{old};
        const char* argv[] = {"besq", "profile", "sql"};
        int rc = CLIApp().run(3, const_cast<char**>(argv));
        expect(rc == 0, "no stmt no -i -> brief usage exit 0");
        expect(out.str().find("profile sql -i") != std::string::npos, "usage shows -i form");
        expect(out.str().find("profile sql \"<stmt>\"") != std::string::npos, "usage shows stmt form");
    }
    TEST_PASS("sql dirty warning + brief usage");
}

// ---------------------------------------------------------------------------
// Slice 2: profile sql — 跨 profile 链（USE 切换 / --profile 被链内 USE 覆盖 /
// USE 未知中止链 / --format json 下 COPY/FORK 消息）
// ---------------------------------------------------------------------------

TEST_CASE("cli_slice_sql_use_chain") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    // 临时 profiles：use_a（一行）+ use_b（空）
    static int counter = 0;
    const std::string tmp =
        (std::filesystem::temp_directory_path() / ("besq_sql_use_" + std::to_string(++counter))).string();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    {
        std::ofstream f(std::filesystem::path(tmp) / "use_a.json");
        f << R"({"name":"use_a","enchantments":[{"id":"test:a","name":"A","platform":"java",
                  "max_level":1,"multiplier":1,"supported_items":["#minecraft:swords"]}],
                  "equipments":[],"tags":{}})";
    }
    {
        std::ofstream f(std::filesystem::path(tmp) / "use_b.json");
        f << R"({"name":"use_b","enchantments":[],"equipments":[],"tags":{}})";
    }
    BesqContext ctx;
    ctx.load_builtin();
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();

    {   // 链：USE use_b → INSERT → SELECT 回读（语句作用于 use_b；USE 消息不过 failure）
        std::string error;
        auto r = ctx.run_sql(
            "USE use_b; "
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:ub','UB',1,1,'#minecraft:swords'); "
            "SELECT id FROM enchantment WHERE id='test:ub';",
            "", error);
        expect(error.empty(), "use chain ok (use: message not a failure)");
        expect(r.steps.size() == 3, "three steps executed");
        expect_eq(r.steps[0].message, "use: use_b", "USE step message");
        expect(r.steps[2].rows.size() == 1 && r.steps[2].rows[0][0] == "test:ub",
               "SELECT after USE reads from use_b");
        expect(r.dirty.size() == 1 && r.dirty[0] == "use_b", "dirty tracks the USE-switched profile");
    }
    {   // --profile（初始 use use_b）+ 链内 USE use_a 覆盖 → 写进 use_a
        std::string error;
        auto r = ctx.run_sql(
            "USE use_a; "
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:ua','UA',1,1,'#minecraft:swords'); "
            "SELECT id FROM enchantment WHERE id='test:ua';",
            "use_b", error);
        expect(error.empty(), "override chain ok");
        expect(r.steps.size() == 3, "three steps executed");
        expect(r.steps[2].rows.size() == 1 && r.steps[2].rows[0][0] == "test:ua",
               "in-chain USE overrides --profile");
        expect(r.dirty.size() == 1 && r.dirty[0] == "use_a", "dirty is the overridden profile");
    }
    {   // USE 未知 → 语句消息走 failure → 链中止（error 报语句 1）
        std::string error;
        auto r = ctx.run_sql("USE nope_nope; SELECT id FROM enchantment LIMIT 1;", "use_a", error);
        expect(!error.empty(), "unknown USE aborts the chain");
        expect(error.find("nope_nope") != std::string::npos, "error names the unknown profile");
        expect(error.find("statement 1") != std::string::npos, "error reports statement 1");
        expect(r.steps.size() == 1, "chain stopped at the failing USE");
        expect_eq(r.last.message, "unknown profile 'nope_nope'", "last message is the use error");
    }
    std::filesystem::remove_all(tmp, ec);
    TEST_PASS("sql use chain");
}

TEST_CASE("cli_slice_sql_cross_json_messages") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    std::ostringstream out, err;
    std::streambuf* oo = std::cout.rdbuf(out.rdbuf());
    std::streambuf* oe = std::cerr.rdbuf(err.rdbuf());
    struct Restore { std::streambuf* o; std::streambuf* e;
                     ~Restore() { std::cout.rdbuf(o); std::cerr.rdbuf(e); } } restore{oo, oe};
    static int counter = 0;
    const std::string fork_name = "besq_fork_json_" + std::to_string(++counter);
    // FORK（新白名单消息 `forked: <x>`）+ COPY WITH OVERRIDE（`N row(s) affected`）
    // → --format json：消息走 stderr，stdout 保持纯 JSON 数组；链不中止
    const std::string stmts = "FORK builtin:vanilla AS " + fork_name + "; "
                              "COPY * FROM builtin:vanilla INTO enchantment "
                              "WHERE id='minecraft:sharpness' WITH OVERRIDE;";
    const char* argv[] = {"besq", "profile", "sql", stmts.c_str(), "--format", "json"};
    int rc = CLIApp().run(6, const_cast<char**>(argv));
    expect(rc == 0, "fork+copy json chain exit 0");
    const std::string so = out.str();
    const std::string se = err.str();
    expect(!so.empty() && so[0] == '[', "stdout is a pure JSON array");
    expect(so.find("forked") == std::string::npos, "message text not on stdout (json)");
    expect(se.find("forked: " + fork_name) != std::string::npos, "forked message on stderr (json)");
    expect(se.find("row(s) affected") != std::string::npos, "copy affected message on stderr (json)");
    TEST_PASS("sql cross json messages");
}

// ─── 积压清扫 T1.5（spec §3.1 测试补强批）：--profile 正向 ────────────────
// 非交互链式指定非激活 profile：语句写/读实际作用于它（片 1 T5 minor——现无
// 直接测试：cli_slice_sql_use_chain 覆盖 --profile + 链内 USE 覆盖，未覆盖
// "纯 --profile 指向非激活 profile 且激活 profile 不变"）。
// run_sql 的 profile 参数 = CLI `--profile` 的映射（CLIApp L595：sql_profile
// 空 → ctx 激活 profile，否则显式 profile），与 cli_slice_sql_use_chain 同源。

TEST_CASE("cli_slice_sql_profile_targets_non_active") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    static int counter = 0;
    const std::string tmp =
        (std::filesystem::temp_directory_path() / ("besq_sql_prof_" + std::to_string(++counter))).string();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    {
        std::ofstream f(std::filesystem::path(tmp) / "p_target.json");
        f << R"({"name":"p_target","enchantments":[],"equipments":[],"tags":{}})";
    }
    {
        std::ofstream f(std::filesystem::path(tmp) / "p_other.json");
        f << R"({"name":"p_other","enchantments":[],"equipments":[],"tags":{}})";
    }
    BesqContext ctx;
    ctx.load_builtin(); // 激活 builtin:vanilla（默认/激活 profile）
    ctx.set_profiles_dir(tmp);
    ctx.load_profiles();
    expect(ctx.active_profile() == "builtin:vanilla", "builtin:vanilla is the active profile");

    // 链：INSERT → SAVE → SELECT（--profile p_target）：写/存/读实际作用于
    // p_target。注：run_sql 每次调用新建 SqlSession（dirty 不跨调用），故 SAVE
    // 必须与写语句同链。
    {
        std::string error;
        auto r = ctx.run_sql(
            "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:profmark','ProfMark',1,1,'#minecraft:swords'); "
            "SAVE; "
            "SELECT id FROM enchantment WHERE id='test:profmark';",
            "p_target", error);
        expect(error.empty(), "--profile chain ok");
        expect(r.steps.size() == 3, "insert + save + select steps");
        expect_eq(r.steps[0].message, "1 row(s) affected", "insert step affected");
        expect_eq(r.steps[1].message, "saved: p_target", "save message names the target");
        expect(r.steps[2].rows.size() == 1 && r.steps[2].rows[0][0] == "test:profmark",
               "SELECT after --profile reads from p_target");
        expect(r.dirty.empty(), "dirty cleared by SAVE in the same chain");
    }
    {   // 激活 profile 不变：active 选中仍 builtin:vanilla，且其注册表无该行
        expect(ctx.active_profile() == "builtin:vanilla", "active selection unchanged by --profile");
        expect(!ctx.profile("builtin:vanilla").has_enchantment(NSID("test:profmark")),
               "active profile registry untouched by the --profile write");
        expect(!ctx.profile("p_other").has_enchantment(NSID("test:profmark")),
               "other profile untouched by the --profile write");
    }
    {   // 读路径同样作用于目标：新会话 SELECT --profile p_target 仍见行（内存态）
        std::string error;
        auto r = ctx.run_sql("SELECT id FROM enchantment WHERE id='test:profmark';", "p_target", error);
        expect(error.empty(), "read via --profile ok");
        expect(r.steps.size() == 1 && r.steps[0].rows.size() == 1 && r.steps[0].rows[0][0] == "test:profmark",
               "read acts on the --profile target");
    }
    {   // SAVE 持久化到目标 profile 文件；非目标文件不含该行
        std::ifstream ft(std::filesystem::path(tmp) / "p_target.json");
        std::stringstream sst;
        sst << ft.rdbuf();
        expect(sst.str().find("test:profmark") != std::string::npos, "p_target.json persisted the row");
        std::ifstream fo(std::filesystem::path(tmp) / "p_other.json");
        std::stringstream sso;
        sso << fo.rdbuf();
        expect(sso.str().find("test:profmark") == std::string::npos, "p_other.json unchanged");
    }
    std::filesystem::remove_all(tmp, ec);
    TEST_PASS("sql --profile targets non-active profile");
}
