#define BESQ_TEST_MAIN

#include "framework/test_framework.h"
#include "domain/orchestration/pipelines/ExportPipeline.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/orchestration/types/ExportRequest.h"
#include "domain/orchestration/types/ExportResult.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Solution.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/components/FormatDetector.h"
#include "common/io/json.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ─── Test 8: export registry as JSON ───────────────────────────────

TEST_CASE("test_export_registry_json") {
    Profile profile("test");

    // Set up a sword equipment tag
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});

    // Add an enchantment
    profile.add_enchantment(EnchInfo{
        NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
        1, false,
        std::unordered_set<NSID>{},                           // no exclusive set
        std::unordered_set<NSID>{EquipmentTag::sword()}       // applicable to swords
    });

    // Add an equipment piece
    profile.add_equipment(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });

    ExportRequest req;
    req.target = ExportRequest::TargetType::Registry;
    req.format = ExportRequest::Format::Json;
    req.output_path = "";

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_registry_json: should succeed");
    expect(!result.content.empty(), "export_registry_json: content should be non-empty");

    std::cout << "PASS: test_export_registry_json" << std::endl;
}

// ─── Test 9: export registry as CSV ────────────────────────────────

TEST_CASE("test_export_registry_csv") {
    Profile profile("test");

    // Set up a sword equipment tag
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});

    // Add an enchantment
    profile.add_enchantment(EnchInfo{
        NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
        1, false,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{EquipmentTag::sword()}
    });

    ExportRequest req;
    req.target = ExportRequest::TargetType::Registry;
    req.format = ExportRequest::Format::Csv;
    req.output_path = "";

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_registry_csv: should succeed");
    expect(!result.content.empty(), "export_registry_csv: content should be non-empty");

    std::cout << "PASS: test_export_registry_csv" << std::endl;
}

// ─── Test 10: export solution ──────────────────────────────────────

TEST_CASE("test_export_solution") {
    Profile profile("test");

    // Set up a sword equipment tag
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});

    // Add the sword equipment (needed by OutputFormatter for name resolution)
    profile.add_equipment(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });

    // Build a trivial solution
    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost = 5;
    solution.target_item = Item(
        NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561
    );

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost = 5;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    solution.steps.push_back(step);

    ExportRequest req;
    req.target = ExportRequest::TargetType::Solution;
    req.format = ExportRequest::Format::Verbose;
    req.output_path = "";
    req.solutions = {solution};

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_solution: should succeed");
    expect(!result.content.empty(), "export_solution: content should be non-empty");

    std::cout << "PASS: test_export_solution" << std::endl;
}

// ─── Test 11: CSV export → import roundtrip ────────────────────────

TEST_CASE("test_csv_export_import_roundtrip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "besq_csv_rt";
    fs::create_directories(dir);
    auto csv_path = dir / "reg.csv";

    // 导出
    Profile profile("test_rt");
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});
    // MCE::All：旧 platform_to_string 导出 "unknown"（往返损坏）；修复后导出 "all"。
    profile.add_enchantment(EnchInfo{
        NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
        std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::sword()}});
    profile.add_equipment(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    bool ok = EnchSerializer::export_csv(csv_path.string(), profile);
    expect(ok, "csv_rt: export ok");
    expect(fs::exists(dir / "equipments_reg.csv"), "csv_rt: companion file written");

    // 导入（FormatDetector::parse → DTO）
    auto parsed = FormatDetector::parse(csv_path);
    expect_eq(static_cast<int>(parsed.enchantments.size()), 1, "csv_rt: 1 ench imported");
    expect_eq(static_cast<int>(parsed.equipment.size()), 1, "csv_rt: 1 eq imported from companion");
    expect(parsed.enchantments[0].platform == "all", "csv_rt: platform roundtrip (All → 'all')");
    expect(parsed.equipment[0].id == "minecraft:diamond_sword", "csv_rt: eq id roundtrip");
    fs::remove_all(dir);
    TEST_PASS("test_csv_export_import_roundtrip");
}

// ─── Test 12: Solution+Json 元数据透传到 JSON root ────────────────────

TEST_CASE("test_export_solution_json_metadata") {
    Profile profile("test");

    // Set up a sword equipment tag
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});

    // Add the sword equipment (needed by OutputFormatter for name resolution)
    profile.add_equipment(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });

    // Build a trivial solution
    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost = 5;
    solution.target_item = Item(
        NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561
    );

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost = 5;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    solution.steps.push_back(step);

    ExportRequest req;
    req.target = ExportRequest::TargetType::Solution;
    req.format = ExportRequest::Format::Json;
    req.output_path = "";
    req.solutions = {solution};
    req.success = false;
    req.algorithm_used = "dp_merge";
    req.computation_time_ms = 42;

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_solution_json_metadata: should succeed");
    expect(!result.content.empty(), "export_solution_json_metadata: content should be non-empty");

    Json root = Json::parse(result.content);
    expect(root.has("success"), "export_solution_json_metadata: root has success");
    expect_eq(root["success"].as_bool(), false, "export_solution_json_metadata: success=false");
    expect_eq(root["algorithm"].as_string(), "dp_merge", "export_solution_json_metadata: algorithm=dp_merge");
    expect_eq(root["computation_time_ms"].as_int(), int64_t(42), "export_solution_json_metadata: computation_time_ms=42");
    expect_eq(root["schema_version"].as_string(), "1.1", "export_solution_json_metadata: schema_version=1.1");

    std::cout << "PASS: test_export_solution_json_metadata" << std::endl;
}

// ─── Test 13: format_for_path 扩展名推断 ──────────────────────────────

TEST_CASE("test_export_format_for_path") {
    expect_eq(ExportPipeline::format_for_path("a.csv"), ExportRequest::Format::Csv,
              "format_for_path: .csv → Csv");
    expect_eq(ExportPipeline::format_for_path("a.CSV"), ExportRequest::Format::Csv,
              "format_for_path: .CSV → Csv (case-insensitive)");
    expect_eq(ExportPipeline::format_for_path("a.json"), ExportRequest::Format::Json,
              "format_for_path: .json → Json");
    expect_eq(ExportPipeline::format_for_path("a.txt"), ExportRequest::Format::Json,
              "format_for_path: .txt → Json (default)");
    expect_eq(ExportPipeline::format_for_path("noext"), ExportRequest::Format::Json,
              "format_for_path: noext → Json (default)");

    std::cout << "PASS: test_export_format_for_path" << std::endl;
}

// ─── Test 14: Registry+McOfficial 文件导出到临时目录 ─────────────────

TEST_CASE("test_export_registry_mc_official_file") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "besq_mc_official_p22";
    fs::remove_all(dir);  // clean slate
    fs::create_directories(dir);

    Profile profile("test");
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});
    profile.add_enchantment(EnchInfo{
        NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5,
        1, false,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{EquipmentTag::sword()}
    });

    ExportRequest req;
    req.target = ExportRequest::TargetType::Registry;
    req.format = ExportRequest::Format::McOfficial;
    req.output_path = dir.string();

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_registry_mc_official_file: should succeed");

    auto sharpness = dir / "data" / "minecraft" / "enchantment" / "sharpness.json";
    expect(fs::exists(sharpness),
           "export_registry_mc_official_file: data/minecraft/enchantment/sharpness.json exists");
    expect(fs::file_size(sharpness) > 0,
           "export_registry_mc_official_file: sharpness.json non-empty");

    fs::remove_all(dir);
    std::cout << "PASS: test_export_registry_mc_official_file" << std::endl;
}

// ─── Test 15: Solution+Verbose 文件导出到临时文件 ─────────────────────

TEST_CASE("test_export_solution_verbose_file") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "besq_sol_verbose_p22";
    fs::create_directories(dir);
    auto out = dir / "plan.txt";

    Profile profile("test");
    profile.add_tag(EquipmentTag{EquipmentTag::sword(), "sword"});
    profile.add_equipment(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost = 5;
    solution.target_item = Item(
        NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561
    );

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost = 5;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    solution.steps.push_back(step);

    ExportRequest req;
    req.target = ExportRequest::TargetType::Solution;
    req.format = ExportRequest::Format::Verbose;
    req.output_path = out.string();
    req.solutions = {solution};

    auto result = ExportPipeline::run(profile, req);
    expect(result.success, "export_solution_verbose_file: should succeed");
    expect(fs::exists(out), "export_solution_verbose_file: file exists");
    expect(fs::file_size(out) > 0, "export_solution_verbose_file: file non-empty");

    fs::remove_all(dir);
    std::cout << "PASS: test_export_solution_verbose_file" << std::endl;
}

} // anonymous namespace
