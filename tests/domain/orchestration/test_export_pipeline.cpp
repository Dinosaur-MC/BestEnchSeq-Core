#include "framework/test_utils.h"
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
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ─── Test 8: export registry as JSON ───────────────────────────────

void test_export_registry_json() {
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

void test_export_registry_csv() {
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

void test_export_solution() {
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

void test_csv_export_import_roundtrip() {
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

} // anonymous namespace

int main() {
    try {
        test_export_registry_json();
        test_export_registry_csv();
        test_export_solution();
        test_csv_export_import_roundtrip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
