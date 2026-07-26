// =============================================================================
// CLI Acceptance Tests
//
// Tests CLI argument parsing for all parameter combinations, error handling,
// and edge cases. Validates that new features (--export-registry without
// --target, --max-time, --registry-edit, --config) parse correctly.
// =============================================================================

#include "domain/interface/cli/CLIApp.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "framework/test_utils.h"

#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Test: --export-registry without --target is valid
// ---------------------------------------------------------------------------

void test_export_only_valid() {
    {
        const char* argv[] = {"besq", "--export-registry", "out.json"};
        auto config = parse_cli(3, const_cast<char**>(argv));
        expect(config.target.empty(), "target should be empty");
        expect(config.export_registry.has_value(), "export_registry should be set");
        expect(*config.export_registry == "out.json", "path should match");
        TEST_PASS("--export-registry only (no --target)");
    }
    {
        const char* argv[] = {"besq", "--export-registry", "out.csv", "--verbose"};
        auto config = parse_cli(4, const_cast<char**>(argv));
        expect(config.target.empty(), "target should be empty with --verbose");
        expect(config.verbose, "verbose should be set");
        expect(config.export_registry.has_value(), "export_registry should be set");
        TEST_PASS("--export-registry + --verbose (no --target)");
    }
}

// ---------------------------------------------------------------------------
// Test: Missing both --target and --export-registry is an error
// ---------------------------------------------------------------------------

void test_missing_target_and_export_errors() {
    {
        const char* argv[] = {"besq", "--algorithm", "greedy"};
        expect_throws([&] { parse_cli(3, const_cast<char**>(argv)); },
                      "Must throw when both --target and --export-registry missing");
        TEST_PASS("no --target and no --export-registry throws");
    }
    {
        const char* argv[] = {"besq", "--verbose", "--format", "json"};
        expect_throws([&] { parse_cli(4, const_cast<char**>(argv)); },
                      "Must throw with flags only, no target");
        TEST_PASS("flags only (no target/export) throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --max-time parsing
// ---------------------------------------------------------------------------

void test_max_time_parsing() {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "30"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.max_time == 30, "max_time should be 30");
        TEST_PASS("--max-time 30");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "0"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.max_time == 0, "max_time should be 0 (unlimited)");
        TEST_PASS("--max-time 0");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "-1"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Negative --max-time should throw");
        TEST_PASS("--max-time negative throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--max-time", "abc"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Non-numeric --max-time should throw");
        TEST_PASS("--max-time non-numeric throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --config validation
// ---------------------------------------------------------------------------

void test_config_parsing() {
    // Valid configs
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "ignore-cost-cap=true"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(!config.config_pairs.empty(), "config should be non-empty");
        TEST_PASS("--config ignore-cost-cap=true");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "ignore-cost-cap=true,ignore-penalty-cost=false"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(!config.config_pairs.empty(), "multi-config should be non-empty");
        TEST_PASS("--config multiple pairs");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "ignore-repair-cost=true"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(!config.config_pairs.empty(), "repair-cost config should be valid");
        TEST_PASS("--config ignore-repair-cost=true");
    }

    // Invalid configs
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", ""};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Empty --config should throw");
        TEST_PASS("--config empty throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "unknown-key=true"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Unknown --config key should throw");
        TEST_PASS("--config unknown key throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "ignore-cost-cap=maybe"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Invalid --config value should throw");
        TEST_PASS("--config invalid value throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--config", "badformat"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Malformed --config should throw");
        TEST_PASS("--config malformed throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --registry-edit parsing
// ---------------------------------------------------------------------------

void test_registry_edit_parsing() {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--registry-edit", "ench:mod,sharpness,max_level=10"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.registry_edit.has_value(), "registry_edit should be set");
        TEST_PASS("--registry-edit valid ench:mod");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--registry-edit", "ench:add,custom:foo,multiplier=3,max_level=5;eq:rm,diamond_sword"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.registry_edit.has_value(), "multi-edit should be set");
        TEST_PASS("--registry-edit multiple ops");
    }
    {
        // Missing colon in operation header
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--registry-edit", "badformat"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Invalid --registry-edit format should throw");
        TEST_PASS("--registry-edit bad format throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword",
                              "--registry-edit", ""};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Empty --registry-edit should throw");
        TEST_PASS("--registry-edit empty throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --algorithm unknown name
// ---------------------------------------------------------------------------

void test_algorithm_name() {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algorithm", "astar"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.algorithm == "astar", "astar algorithm name should be stored");
        TEST_PASS("--algorithm astar");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--algorithm", "nonexistent"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.algorithm == "nonexistent", "unknown algorithm name should still be stored");
        TEST_PASS("--algorithm unknown name (validated later in main)");
    }
}

// ---------------------------------------------------------------------------
// Test: --memory validation
// ---------------------------------------------------------------------------

void test_memory_parsing() {
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "auto"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.memory_mb == 0, "memory=auto should be 0");
        TEST_PASS("--memory auto");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "256"};
        auto config = parse_cli(5, const_cast<char**>(argv));
        expect(config.memory_mb == 256, "memory should be 256");
        TEST_PASS("--memory 256");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "-1"};
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Negative --memory should throw");
        TEST_PASS("--memory negative throws");
    }
    {
        const char* argv[] = {"besq", "--target", "diamond_sword", "--memory", "999999999"};
        // This is > 1048576
        expect_throws([&] { parse_cli(5, const_cast<char**>(argv)); },
                      "Too-large --memory should throw");
        TEST_PASS("--memory too large throws");
    }
}

// ---------------------------------------------------------------------------
// Test: --apply_config_pairs functional test
// ---------------------------------------------------------------------------

void test_apply_config_pairs() {
    algorithm::ForgeConfig cfg;
    cfg.ignore_cost_cap = false;
    cfg.ignore_penalty_cost = false;
    cfg.ignore_repair_cost = false;

    apply_config_pairs("ignore-cost-cap=true", cfg);
    expect(cfg.ignore_cost_cap, "cost cap should be true");
    expect(!cfg.ignore_penalty_cost, "penalty cost should remain false");
    expect(!cfg.ignore_repair_cost, "repair cost should remain false");

    apply_config_pairs("ignore-penalty-cost=true,ignore-repair-cost=true", cfg);
    expect(cfg.ignore_penalty_cost, "penalty cost should now be true");
    expect(cfg.ignore_repair_cost, "repair cost should now be true");

    // Reset
    cfg.ignore_cost_cap = false;
    apply_config_pairs("ignore-cost-cap=false", cfg);
    expect(!cfg.ignore_cost_cap, "cost cap should be false again");

    TEST_PASS("apply_config_pairs functional");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        test_export_only_valid();
        test_missing_target_and_export_errors();
        test_max_time_parsing();
        test_config_parsing();
        test_registry_edit_parsing();
        test_algorithm_name();
        test_memory_parsing();
        test_apply_config_pairs();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }

    return print_summary();
}
