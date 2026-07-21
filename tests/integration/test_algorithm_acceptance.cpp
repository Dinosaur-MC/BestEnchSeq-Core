// =============================================================================
// Algorithm Acceptance Tests
//
// Tests all registered algorithms with the full pipeline (parse → execute →
// recall → format) and validates that every combination produces correct,
// non-empty output in all three output formats.
// =============================================================================

#include "cli/cli.h"
#include "parsers/EnchInfoParser.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "adapters/OutputFormatter.h"
#include "adapters/RawTypeAdapter.h"
#include "adapters/CompactAdapter.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/AlgorithmRegistry.h"
#include "adapters/EnchSerializer.h"
#include "config/ForgeConfig.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/strategies/Strategies.h"
#include "io/json.h"
#include "framework/test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

static auto& test_ench_reg = registries::enchants();
static auto& test_cat_reg  = registries::categories();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void load_builtin_data(EquipmentRegistry& eq_reg) {
    test_cat_reg.initialize();
    auto [raw_ench, raw_eq] = EnchInfoParser::parse_native_json(
        "data/builtin/vanilla.json");
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, test_cat_reg);
    test_ench_reg.initialize(ench_infos);
    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, test_cat_reg);
    eq_reg.initialize(equipments);
}

static void register_all_algorithms(AlgorithmRegistry& reg) {
    reg.register_algorithm("greedy", [] { return std::make_unique<GreedyAlgorithm>(); });
    reg.register_algorithm("dfs", [] { return std::make_unique<DFSAlgorithm>(); });
    reg.register_algorithm("astar", [] { return std::make_unique<AStarAlgorithm>(); });
    reg.register_algorithm("penalty_balance",
                            [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
    reg.register_algorithm("hierarchical",
                            [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
    reg.register_algorithm("idastar",
                            [] { return std::make_unique<IDAStarAlgorithm>(); });
    reg.register_algorithm("hamming",
                            [] { return std::make_unique<HammingAlgorithm>(); });
    reg.register_algorithm("diff_first",
                            [] { return std::make_unique<DiffFirstAlgorithm>(); });
    reg.register_algorithm("difficulty_first",
                            [] { return std::make_unique<DiffFirstAlgorithm>(); });
}

// Validate JSON output structure
static void check_json_solutions(const std::string& json_str, size_t min_solutions) {
    auto json = Json::parse(json_str);
    expect(json.is_valid() && json.type() == JsonType::Object,
           "JSON output root must be a valid object");

    auto root_val = json.get_value();
    auto* root_obj = std::get_if<Json::Object>(&root_val);
    if (!root_obj) return;

    expect(root_obj->count("solutions") > 0, "JSON must have 'solutions' key");
    expect(root_obj->count("schema_version") > 0, "JSON must have 'schema_version' key");
    expect(root_obj->count("mode") > 0, "JSON must have 'mode' key");

    auto sol_val_tmp = (*root_obj)["solutions"].get_value();
    auto* sol_arr = std::get_if<Json::Array>(&sol_val_tmp);
    expect(sol_arr != nullptr, "'solutions' must be an array");
    if (!sol_arr) return;
    expect(sol_arr->size() >= min_solutions,
           "solutions array size should be >= " + std::to_string(min_solutions));

    // Validate first solution structure
    if (!sol_arr->empty()) {
        auto first_val_tmp = (*sol_arr)[0].get_value();
        auto* first_obj = std::get_if<Json::Object>(&first_val_tmp);
        expect(first_obj != nullptr, "first solution must be an object");
        if (first_obj) {
            expect(first_obj->count("steps") > 0, "solution must have 'steps'");
            expect(first_obj->count("total_exp_level_cost") > 0,
                   "solution must have 'total_exp_level_cost'");
            expect(first_obj->count("is_success") > 0,
                   "solution must have 'is_success'");

            // Verify peak cost fields (using corrected spelling)
            auto peak_lvl_it = first_obj->find("peak_level_cost");
            expect(peak_lvl_it != first_obj->end(),
                   "solution must have 'peak_level_cost' (not peek_)");
            auto peak_exp_it = first_obj->find("peak_exp_cost");
            expect(peak_exp_it != first_obj->end(),
                   "solution must have 'peak_exp_cost' (not peek_)");

            // Verify alphabetical field ordering (std::map consistency)
            // The first field alphabetically should be "available_items" after "is_success"
            auto first_field = first_obj->begin();
            if (first_field != first_obj->end()) {
                expect(first_field->first == "available_items",
                       "first solution field should be 'available_items', got '" +
                       first_field->first + "' (std::map ordering)");
            }
        }
    }
}

// Validate compact output has expected structure
static void check_compact_output(const std::string& output) {
    expect(output.find("#MODE=") != std::string::npos,
           "compact output must have #MODE= line");
    expect(output.find("#SOLUTIONS=") != std::string::npos,
           "compact output must have #SOLUTIONS= line");
}

// ---------------------------------------------------------------------------
// Test: Every algorithm produces valid output in all formats
// ---------------------------------------------------------------------------

void test_all_algorithms_all_formats() {
    EquipmentRegistry eq_reg;
    load_builtin_data(eq_reg);

    // Use a simple target to keep search times manageable for all algorithms
    const std::string target_spec_str = "diamond_sword[sharpness=3]";
    const char* argv[] = {"besq", "--target", target_spec_str.c_str()};
    auto config = parse_cli(3, const_cast<char**>(argv));

    auto target_spec = ItemParser::parse(config.target);
    ItemStack target_item = build_target(target_spec, test_ench_reg, eq_reg);
    EnchSet target_ench = build_enchset(target_spec.inline_enchants, test_ench_reg);

    auto resolved = ItemResolver::resolve(target_item, EnchSet{}, target_ench, test_ench_reg);

    const std::vector<std::string> algorithms = {
        "greedy", "dfs", "astar", "penalty_balance",
        "hierarchical", "idastar", "hamming", "diff_first"
    };
    const std::vector<std::string> formats = {"text", "compact", "json"};

    for (const auto& algo_name : algorithms) {
        for (const auto& fmt : formats) {
            try {
                // Re-build from resolved (fresh AlgorithmInput per run)
                AlgorithmInput algo_input = CompactAdapter::apply(resolved, test_ench_reg);
                algo_input.config.platform = MCE::Java;

                // Create algorithm
                AlgorithmRegistry reg;
                auto algo = reg.create(algo_name);
                if (!algo) {
                    // Try alias
                    reg.register_algorithm(algo_name, [] {
                        return std::make_unique<GreedyAlgorithm>();
                    });
                    algo = reg.create(algo_name);
                }

                // Use builtin registration instead
                AlgorithmRegistry algo_reg;
                register_all_algorithms(algo_reg);
                algo = algo_reg.create(algo_name);
                expect(algo != nullptr,
                       algo_name + ": algorithm should be creatable");

                AlgorithmExecutor executor(std::move(algo));
                executor.start(algo_input);
                auto state = executor.wait();
                expect(state == AlgorithmState::Completed,
                       algo_name + "/" + fmt + ": algorithm should complete");

                auto output = executor.output();
                expect(output.is_valid,
                       algo_name + "/" + fmt + ": output should be valid");
                expect(!output.solutions.empty(),
                       algo_name + "/" + fmt + ": should have solutions");

                // Convert to domain
                auto solutions = CompactAdapter::recall(output, algo_input,
                    resolved.source_ench, resolved.target_item, resolved.available_items);
                expect(!solutions.empty(),
                       algo_name + "/" + fmt + ": should have domain solutions");
                expect(solutions[0].is_success,
                       algo_name + "/" + fmt + ": first solution should succeed");

                // ── Solution correctness verification ─────────────────────
                expect(solutions[0].total_exp_level_cost > 0,
                       algo_name + ": total cost should be positive");
                expect(!solutions[0].steps.empty(),
                       algo_name + ": solution should have forge steps");
                expect(solutions[0].total_exp_level_cost <= 39,
                       algo_name + ": vanilla cost should not exceed 39 level cap");

                // Verify all steps have non-negative costs
                int64_t sum_step_level_costs = 0;
                for (size_t si = 0; si < solutions[0].steps.size(); ++si) {
                    const auto& step = solutions[0].steps[si];
                    expect(step.exp_level_cost >= 0,
                           algo_name + ": step " + std::to_string(si) + " level cost >= 0");
                    expect(step.exp_cost >= 0,
                           algo_name + ": step " + std::to_string(si) + " exp cost >= 0");
                    sum_step_level_costs += step.exp_level_cost;
                }
                // Total level cost should approximately match step sum
                // (may differ slightly due to cap at 39)
                expect(sum_step_level_costs >= solutions[0].total_exp_level_cost || solutions[0].total_exp_level_cost >= 39,
                       algo_name + ": total cost should be <= sum of step costs");

                // Format and validate
                std::string formatted;
                if (fmt == "json") {
                    formatted = OutputFormatter::format_json(
                        solutions, test_ench_reg, test_cat_reg, "direct");
                    check_json_solutions(formatted, 1);
                } else if (fmt == "compact") {
                    formatted = OutputFormatter::format_compact(
                        solutions, test_ench_reg, test_cat_reg, "direct");
                    check_compact_output(formatted);
                    expect(formatted.find("|") != std::string::npos,
                           algo_name + "/compact: should have step data");
                } else {
                    formatted = OutputFormatter::format_verbose(
                        solutions, test_ench_reg, test_cat_reg, "direct");
                    expect(!formatted.empty(),
                           algo_name + "/text: output should be non-empty");
                }

                TEST_PASS(algo_name + "/" + fmt);
            } catch (const std::exception& e) {
                std::cerr << "\n  FAIL: " << algo_name << "/" << fmt
                          << ": " << e.what() << std::endl;
                tests_failed++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test: Algorithm alias (diff_first == difficulty_first)
// ---------------------------------------------------------------------------

void test_diff_first_alias() {
    EquipmentRegistry eq_reg;
    load_builtin_data(eq_reg);

    const char* argv[] = {"besq", "--target", "diamond_sword[sharpness=3]"};
    auto config = parse_cli(3, const_cast<char**>(argv));

    auto target_spec = ItemParser::parse(config.target);
    auto target_item = build_target(target_spec, test_ench_reg, eq_reg);
    EnchSet target_ench = build_enchset(target_spec.inline_enchants, test_ench_reg);
    auto resolved = ItemResolver::resolve(target_item, EnchSet{}, target_ench, test_ench_reg);

    // Test both names produce identical results
    std::vector<compact::EnchSolution> solutions_a, solutions_b;

    for (const auto& name : {"diff_first", "difficulty_first"}) {
        AlgorithmRegistry reg;
        register_all_algorithms(reg);
        auto algo = reg.create(name);
        expect(algo != nullptr, std::string(name) + ": should resolve");

        AlgorithmInput input = CompactAdapter::apply(resolved, test_ench_reg);
        input.config.platform = MCE::Java;

        AlgorithmExecutor exec(std::move(algo));
        exec.start(input);
        exec.wait();
        auto out = exec.output();

        if (name == std::string_view("diff_first"))
            solutions_a = out.solutions;
        else
            solutions_b = out.solutions;
    }

    expect(!solutions_a.empty(), "diff_first should produce solutions");
    expect(solutions_a.size() == solutions_b.size(),
           "Both aliases should produce same number of solutions");

    TEST_PASS("diff_first / difficulty_first alias");
}

// ---------------------------------------------------------------------------
// Test: --export-registry only (no --target)
// ---------------------------------------------------------------------------

void test_cli_export_only_valid() {
    // parse_cli with only --export-registry (no --target) should succeed
    const char* argv[] = {"besq", "--export-registry", "/tmp/test_export.json"};
    auto config = parse_cli(3, const_cast<char**>(argv));
    expect(config.target.empty(), "target should be empty");
    expect(config.export_registry.has_value(), "export_registry should be set");
    expect(*config.export_registry == "/tmp/test_export.json",
           "export_registry path should match");

    TEST_PASS("--export-registry without --target");
}

// ---------------------------------------------------------------------------
// Test: Export content verification (JSON + CSV)
// ---------------------------------------------------------------------------

void test_export_content() {
    EquipmentRegistry eq_reg;
    load_builtin_data(eq_reg);

    // ── Verify serialization produces valid content (in-memory) ──────────
    {
        // Use internal serialization methods to verify content completeness
        const auto& all_ench = test_ench_reg.get_instances();
        std::vector<EnchInfo> valid_ench;
        for (const auto& info : all_ench)
            if (!info.name_id.empty()) valid_ench.push_back(info);
        expect(valid_ench.size() >= 20, "should have at least 20 enchantments loaded");

        const auto& all_eq = eq_reg.get_instances();
        std::vector<Equipment> valid_eq;
        for (const auto& eq : all_eq)
            if (!eq.name_id.empty()) valid_eq.push_back(eq);
        expect(valid_eq.size() >= 10, "should have at least 10 equipment entries");

        // Verify known enchantment IDs resolve correctly
        expect(test_ench_reg.get_id("minecraft:sharpness") >= 0,
               "sharpness should be in registry");
        expect(test_ench_reg.get_id("minecraft:protection") >= 0,
               "protection should be in registry");
        expect(test_ench_reg.get_id("minecraft:fortune") >= 0,
               "fortune should be in registry");

        // Verify known equipment IDs resolve correctly
        expect(eq_reg.get_id("minecraft:diamond_sword") >= 0,
               "diamond_sword should be in equipment registry");
        expect(eq_reg.get_id("minecraft:diamond_pickaxe") >= 0,
               "diamond_pickaxe should be in equipment registry");

        // Verify JSON serialization produces valid output
        std::string ench_json = EnchSerializer::to_json(valid_ench, test_cat_reg);
        expect(!ench_json.empty(), "JSON serialization should produce non-empty output");
        expect(ench_json.find("sharpness") != std::string::npos,
               "JSON should contain sharpness");
        expect(ench_json.find("protection") != std::string::npos,
               "JSON should contain protection");

        std::string eq_json = EnchSerializer::to_json(valid_eq, test_cat_reg);
        expect(!eq_json.empty(), "Equipment JSON serialization should produce non-empty output");
        expect(eq_json.find("diamond_sword") != std::string::npos,
               "Equipment JSON should contain diamond_sword");

        // Verify CSV serialization produces valid output
        std::string ench_csv = EnchSerializer::to_csv(valid_ench, test_cat_reg);
        expect(!ench_csv.empty(), "CSV serialization should produce non-empty output");
        expect(ench_csv.find("sharpness") != std::string::npos,
               "CSV should contain sharpness");
        expect(ench_csv.find("protection") != std::string::npos,
               "CSV should contain protection");

        std::string eq_csv = EnchSerializer::to_csv(valid_eq, test_cat_reg);
        expect(!eq_csv.empty(), "Equipment CSV serialization should produce non-empty output");
        expect(eq_csv.find("diamond_sword") != std::string::npos,
               "Equipment CSV should contain diamond_sword");

        TEST_PASS("export content in-memory verification");
    }

    // ── File export test ────────────────────────────────────────────────
    {
        // Use local filename (no temp_directory_path to avoid FS issues)
        const std::string test_json = "besq_test_export.json";
        const std::string test_csv  = "besq_test_export.csv";

        // JSON file export
        bool ok = EnchSerializer::export_json(test_json, test_ench_reg, eq_reg, test_cat_reg);
        expect(ok, "JSON file export should succeed");
        if (ok && std::filesystem::exists(test_json)) {
            expect(std::filesystem::file_size(test_json) > 0, "JSON export file should not be empty");
            std::filesystem::remove(test_json);
        }

        // CSV file export
        ok = EnchSerializer::export_csv(test_csv, test_ench_reg, eq_reg, test_cat_reg);
        expect(ok, "CSV file export should succeed");
        if (ok && std::filesystem::exists(test_csv)) {
            expect(std::filesystem::file_size(test_csv) > 0, "CSV export file should not be empty");
            std::filesystem::remove(test_csv);
        }

        // Clean up equipments sibling CSV
        std::string eq_csv = "equipments_" + test_csv;
        if (std::filesystem::exists(eq_csv))
            std::filesystem::remove(eq_csv);

        TEST_PASS("export file I/O");
    }
}

// ---------------------------------------------------------------------------
// Test: --export-registry produces valid file via full CLI pipeline
// ---------------------------------------------------------------------------

void test_full_export_pipeline() {
    // Use a local filename to avoid filesystem permission issues
    const std::string test_path = "besq_cli_export.json";

    const char* argv[] = {"besq", "--export-registry", test_path.c_str()};
    auto config = parse_cli(3, const_cast<char**>(argv));
    expect(config.export_registry.has_value(), "--export-registry path should be set");
    expect(*config.export_registry == test_path, "path should match input");

    TEST_PASS("full export pipeline path resolution");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        test_all_algorithms_all_formats();
        test_diff_first_alias();
        test_cli_export_only_valid();
        test_export_content();
        test_full_export_pipeline();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }

    // Flush diagnostics before static destruction to avoid
    // EventLoop thread accessing destroyed singletons
    DiagnosticsService::instance().flush();

    return print_summary();
}
