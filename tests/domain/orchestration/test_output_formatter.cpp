#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/orchestration/orchestration.h"
#include "domain/orchestration/components/OutputSchema.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/business/parsers/ParserShared.h"
#include "common/i18n/Language.h"
#include "common/i18n/NsidDisplay.h"
#include "common/io/json.h"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

static TestFixture g_fx;

namespace {

// Helper: build a Profile from the TestFixture's registries
Profile profile_from_fx(const TestFixture& fx) {
    Profile profile("test:formatter");
    for (const auto& tag : fx.categories) profile.add_tag(tag);
    for (const auto& eq : fx.equipment) profile.add_equipment(eq);
    for (const auto& ench : fx.enchants) profile.add_enchantment(ench);
    return profile;
}

// ─── Test 1: format a simple book solution ─────────────────────────

void test_format_book_solution() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost = 5;

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost = 5;
    step.item_a = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);

    // Need to construct a valid solution with target equipment
    solution.steps.push_back(step);
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, profile, AlgorithmMode::direct);
    expect(!formatted.empty(), "format: should produce non-empty output");

    std::cout << "PASS: test_format_book_solution" << std::endl;
}

// ─── Test 2: format a combined solution with raw adapter ────────────

void test_format_combined_solution() {
    g_fx.init_chestplate_set();
    auto profile = profile_from_fx(g_fx);
    EnchantmentRegistry& enchants = g_fx.enchants;

    // Create a protection 3 book
    EnchSet enchants_set;
    const auto& prot_info = enchants.at(NSID("protection"));
    enchants_set.emplace(prot_info.id, prot_info.name, 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_chestplate"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 528
    );

    Solution::EnchStep step;
    step.exp_level_cost = 3;
    step.exp_cost = 3;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), enchants_set, 0);
    solution.steps.push_back(step);
    solution.total_exp_level_cost = 3;
    solution.total_exp_cost = 3;

    auto formatted = OutputFormatter::format_compact(
        {solution}, profile, AlgorithmMode::direct);
    expect(!formatted.empty(), "format_combined: should produce non-empty output");

    std::cout << "PASS: test_format_combined_solution" << std::endl;
}

// ─── Test 3: format with no steps (edge case) ──────────────────────

void test_format_no_steps() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, profile, AlgorithmMode::direct);
    expect(!formatted.empty(), "format_no_steps: should still produce output");

    std::cout << "PASS: test_format_no_steps" << std::endl;
}

// ─── Test 4: format with unsuccesful solution (zero steps) ─────────

void test_format_unsuccessful() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = false;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, profile, AlgorithmMode::direct);
    expect(!formatted.empty(), "format_unsuccessful: should still produce output");

    std::cout << "PASS: test_format_unsuccessful" << std::endl;
}

// ─── Test 5: format with multiple steps ─────────────────────────────

void test_format_multi_step() {
    g_fx.init_chestplate_set();
    auto profile = profile_from_fx(g_fx);
    EnchantmentRegistry& enchants = g_fx.enchants;

    EnchSet prot3;
    const auto& prot_info = enchants.at(NSID("protection"));
    prot3.emplace(prot_info.id, prot_info.name, 3);

    EnchSet unbr3;
    const auto& unbr_info = enchants.at(NSID("unbreaking"));
    unbr3.emplace(unbr_info.id, unbr_info.name, 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_chestplate"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 528
    );

    // Step 1: add protection 3
    Solution::EnchStep step1;
    step1.exp_level_cost = 3;
    step1.exp_cost = 3;
    step1.item_a = solution.target_item;
    step1.item_b = Item(NSID("minecraft:enchanted_book"), prot3, 0);
    solution.steps.push_back(step1);

    // Step 2: add unbreaking 3
    Solution::EnchStep step2;
    step2.exp_level_cost = 3;
    step2.exp_cost = 3;
    step2.item_a = step1.item_a;
    step2.item_b = Item(NSID("minecraft:enchanted_book"), unbr3, 0);
    solution.steps.push_back(step2);

    solution.total_exp_level_cost = 6;
    solution.total_exp_cost = 6;

    auto formatted = OutputFormatter::format_compact(
        {solution}, profile, AlgorithmMode::direct);
    expect(!formatted.empty(), "format_multi: should produce output");

    // Multi-step output should be non-empty (size comparison removed as
    // format_compact output varies from the old format() API)
    std::cout << "PASS: test_format_multi_step" << std::endl;
}

// ─── Test 6: verbose item format with new simplified format ──────────

void test_verbose_item_format() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution sol;
    sol.is_success = true;
    sol.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    sol.target_item = Item(equip.id, EnchSet{}, 0, 1561);
    sol.total_exp_level_cost = 0;
    sol.total_exp_cost = 0;

    // Step with empty items (should show as free)
    Solution::EnchStep step;
    step.exp_level_cost = 0;
    step.exp_cost = 0;
    step.item_a = sol.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    sol.steps.push_back(step);

    auto output = OutputFormatter::format_verbose({sol}, profile, AlgorithmMode::direct);

    // Check for new format patterns
    expect(output.find("{ppn=0,dur=1561}") != std::string::npos,
           "verbose output should have new attribute format with ppn and dur");
    // Note: tr() returns the key string when Language is uninitialized in tests
    expect(output.find("output.item.free") != std::string::npos ||
           output.find("(free)") != std::string::npos,
           "verbose output should show free indicator for empty items with ppn=0");
    expect(output.find("enchanted_book") != std::string::npos,
           "verbose output should show 'enchanted_book' for books");

    TEST_PASS("test_verbose_item_format");
}

// ─── Test 8: verbose format with final_item ──────────────────────────

void test_format_verbose_final_item() {
    g_fx.init_chestplate_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_chestplate"));
    solution.target_item = Item(equip.id, EnchSet{}, 0, 528);
    solution.total_exp_level_cost = 0;
    solution.total_exp_cost = 0;

    Solution::EnchStep step;
    step.exp_level_cost = 0;
    step.exp_cost = 0;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    solution.steps.push_back(step);

    // Set final_item (simulating what Task 6 provides)
    solution.final_item = Item(equip.id, EnchSet{}, 1, 528);

    auto output = OutputFormatter::format_verbose({solution}, profile, AlgorithmMode::direct);
    // Note: tr() returns the key when Language is not initialized in tests
    expect(output.find("output.verbose.final_item") != std::string::npos,
           "verbose output should contain Final Item section");
    expect(output.find("diamond_chestplate") != std::string::npos,
           "final item should reference the correct equipment");

    std::cout << "PASS: test_format_verbose_final_item" << std::endl;
}

// ─── Test 9: verbose format with too expensive warning ───────────────

void test_format_verbose_too_expensive() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(equip.id, EnchSet{}, 0, 1561);
    solution.total_exp_level_cost = 45;
    solution.total_exp_cost = 45;

    Solution::EnchStep step;
    step.exp_level_cost = 45;  // over 39 threshold
    step.exp_cost = 45;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    solution.steps.push_back(step);
    solution.max_cost_step_index = 0;

    auto output = OutputFormatter::format_verbose({solution}, profile, AlgorithmMode::direct);
    // Note: tr() returns the key itself when Language is not initialized in tests
    expect(output.find("output.verbose.too_expensive") != std::string::npos,
           "verbose output should warn about too expensive");

    std::cout << "PASS: test_format_verbose_too_expensive" << std::endl;
}

// ─── Test: format_json emits real equipment data + C ABI-aligned root ──

void test_format_json_real_equipment() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.metadata.algorithm_name = "dp_merge";
    solution.metadata.computation_time = std::chrono::milliseconds(42);
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(equip.id, EnchSet{}, 0, equip.max_durability);
    solution.total_exp_level_cost = 0;
    solution.total_exp_cost = 0;

    auto json_str = OutputFormatter::format_json(
        {solution}, profile, AlgorithmMode::direct, true, "dp_merge", 42);
    Json root = Json::parse(json_str);

    // Root metadata aligned with the C ABI (besq_solve).
    expect(root["success"].as<bool>() == true, "format_json root: success");
    expect(root["algorithm"].as<std::string>() == "dp_merge",
           "format_json root: algorithm");
    expect(root["computation_time_ms"].as<int64_t>() == 42,
           "format_json root: computation_time_ms");
    expect(root["mode"].as<std::string>() == "direct", "format_json root: mode");

    // Real equipment durability/category instead of 0 / "unknown".
    Json target = root["solutions"][0]["target_item"];
    expect(target["is_book"].as<bool>() == false, "format_json: target is not a book");
    expect(target["equipment"]["id"].as<std::string>() == "minecraft:diamond_sword",
           "format_json: equipment id");
    expect(target["equipment"]["max_durability"].as<int32_t>() == 1561,
           "format_json: real max_durability");
    expect(target["equipment"]["category"].as<std::string>() == "sword",
           "format_json: real category short name");

    TEST_PASS("format_json real equipment + root metadata");
}

// ─── Test: format_json steps carry the forged result item (A+B=C) ─────
// The calculator result area renders each step as an A+B=C card; the C
// (forged result item) must be present in the step JSON.

void test_format_json_step_result() {
    g_fx.init_chestplate_set();
    auto profile = profile_from_fx(g_fx);
    EnchantmentRegistry& enchants = g_fx.enchants;

    // Protection 3 sacrifice book
    EnchSet prot3;
    const auto& prot_info = enchants.at(NSID("protection"));
    prot3.emplace(prot_info.id, prot_info.name, 3);

    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_chestplate"));

    Solution solution;
    solution.is_success = true;
    solution.platform    = MCE::Java;
    solution.target_item = Item(equip.id, EnchSet{}, 0, equip.max_durability);
    solution.total_exp_level_cost = 3;
    solution.total_exp_cost       = 3;

    Solution::EnchStep step;
    step.exp_level_cost = 3;
    step.exp_cost       = 3;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), prot3, 0);
    // Forged result: the chestplate now carries protection 3
    step.result = Item(equip.id, prot3, 0, equip.max_durability);
    solution.steps.push_back(step);

    const auto json = OutputFormatter::format_json({solution}, profile, AlgorithmMode::direct);
    Json root = Json::parse(json);
    Json step0 = root["solutions"][0]["steps"][0];

    expect(step0.has("result"), "format_json: step carries result item (A+B=C)");
    if (!step0.has("result"))
        return;  // avoid crashing below when result is missing

    Json res = step0["result"];
    expect(res["is_book"].as<bool>() == false,
           "format_json: step result is forged equipment, not a book");
    expect(res["equipment"]["id"].as<std::string>() == "minecraft:diamond_chestplate",
           "format_json: step result keeps the target equipment id");
    expect(res["enchantments"].as_array().size() == 1,
           "format_json: step result carries the forged enchantments");
    expect(res["enchantments"][0]["id"].as<std::string>() == prot_info.id.str(),
           "format_json: step result carries the forged enchantment id");

    TEST_PASS("format_json step result item");
}

// ─── Test: compact #PLATFORM uses raw enum name (never localized) ─────
// B-T25: compact output is machine-readable and must NOT localize the
// platform name.  Register a zh_CN table where the display string is
// "Java版" to prove the compact output still emits `#PLATFORM=Java`.

void test_format_compact_platform_raw() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(equip.id, EnchSet{}, 0, 1561);

    auto& lm = LanguageManager::instance();

    // Register minimal platform tables for both locales (only if absent).
    bool has_cn = false, has_en = false;
    for (const auto& code : lm.available()) {
        if (code == "zh_CN") has_cn = true;
        if (code == "en_US") has_en = true;
    }
    if (!has_cn) {
        Language::Table cn;
        cn["output.platform.java"]    = "Java版";
        cn["output.platform.bedrock"] = "基岩版";
        cn["output.platform.all"]     = "全部";
        cn["output.platform.unknown"] = "未知";
        lm.register_language(Language("zh_CN", std::move(cn)));
    }
    if (!has_en) {
        Language::Table en;
        en["output.platform.java"]    = "Java";
        en["output.platform.bedrock"] = "Bedrock";
        en["output.platform.all"]     = "All";
        en["output.platform.unknown"] = "Unknown";
        lm.register_language(Language("en_US", std::move(en)));
    }

    // zh_CN: display string is "Java版" — compact must stay raw.
    lm.select("zh_CN");
    auto zh = OutputFormatter::format_compact({solution}, profile, AlgorithmMode::direct);
    expect(zh.find("#PLATFORM=Java\n") != std::string::npos,
           "compact zh_CN: emits #PLATFORM=Java (raw enum name)");
    expect(zh.find("Java版") == std::string::npos,
           "compact zh_CN: must NOT leak localized platform name");

    // en_US: same raw name.
    lm.select("en_US");
    auto en = OutputFormatter::format_compact({solution}, profile, AlgorithmMode::direct);
    expect(en.find("#PLATFORM=Java\n") != std::string::npos,
           "compact en_US: emits #PLATFORM=Java (raw enum name)");

    TEST_PASS("test_format_compact_platform_raw");
}

// ─── JSON round-trip: format_json → parse_json ─────────────────────────

void test_json_roundtrip() {
    g_fx.init_sword_set();
    auto profile = profile_from_fx(g_fx);

    Solution solution;
    solution.is_success = true;
    solution.platform    = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost       = 5;
    solution.metadata.algorithm_name = "hamming";

    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_sword"));

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost       = 5;
    step.item_a = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    step.item_b = Item(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    // Forged result: the sword with prior penalty 1
    step.result = Item(equip.id, EnchSet{}, 1, 1561);
    solution.steps.push_back(step);

    solution.target_item = Item(equip.id, EnchSet{}, 0, 1561);

    const auto json = OutputFormatter::format_json({solution}, profile, AlgorithmMode::direct);
    const auto parsed = OutputFormatter::parse_json(json, profile);

    expect_eq(parsed.size(), 1u, "round-trip: one solution parsed");
    if (parsed.size() != 1)
        return;  // avoid crashing on a malformed parse below
    expect(parsed[0].is_success, "round-trip: is_success preserved");
    expect_eq(parsed[0].steps.size(), 1u, "round-trip: step count preserved");
    expect(parsed[0].steps[0].result.id == NSID("minecraft:diamond_sword"),
           "round-trip: step result item preserved");
    expect_eq(parsed[0].steps[0].result.prior_penalty, 1,
              "round-trip: step result prior penalty preserved");
    expect_eq(parsed[0].total_exp_level_cost, 5, "round-trip: total level cost");
    expect(parsed[0].target_item.id == NSID("minecraft:diamond_sword"),
           "round-trip: target equipment preserved");
    TEST_PASS("OutputFormatter JSON round-trip");
}

// ─── Reference builders: the frozen hand-built wire shape ────────────────
// These mirror the pre-schema JSON assembly of OutputFormatter exactly
// (schema_version is the only versioned field: 1.1 now).  The schema-driven
// encode must reproduce them byte-for-byte after map-sorted serialization.

Json reference_item_json(const Item &item, const TagRegistry &cat_reg,
                         const EquipmentRegistry &eq_reg) {
    Json::Object obj;

    if (!item.is_book()) {
        Json::Object eq;
        eq["id"]             = Json(item.id.str());
        eq["category"]       = Json("unknown");
        eq["name"]           = Json(item_display_name(item.id));
        eq["max_durability"] = Json(0);
        if (auto eq_it = eq_reg.find(item.id); eq_it != eq_reg.end()) {
            eq["max_durability"] = Json(eq_it->max_durability);
            std::string category_name;
            if (auto tag_it = cat_reg.find(eq_it->category);
                tag_it != cat_reg.end()) {
                category_name = tag_it->name;
            } else {
                category_name =
                    business::parser_detail::category_short_name(eq_it->category);
                if (category_name.empty())
                    category_name = "unknown";
            }
            eq["category"] = Json(std::move(category_name));
        }
        obj["equipment"] = Json(eq);
        obj["is_book"]   = Json(false);
    } else {
        obj["equipment"] = Json::null();
        obj["is_book"]   = Json(true);
    }

    Json::Array ench_arr;
    for (const auto &ench : item.enchantments) {
        Json::Object eo;
        eo["id"]    = Json(ench.id.str());
        eo["level"] = Json(ench.level);
        ench_arr.push_back(Json(eo));
    }
    obj["enchantments"] = Json(ench_arr);

    obj["prior_penalty"] = Json(item.prior_penalty);
    obj["durability"]    = Json(item.durability);
    return Json(obj);
}

Json reference_solution_json(const Solution &sol, int32_t rank,
                             const TagRegistry &cat_reg,
                             const EquipmentRegistry &eq_reg) {
    Json::Object s;
    s["rank"] = Json(rank);
    switch (sol.platform) {
    case MCE::Java:    s["platform"] = Json("Java");    break;
    case MCE::Bedrock: s["platform"] = Json("Bedrock"); break;
    case MCE::All:     s["platform"] = Json("All");     break;
    default:           s["platform"] = Json("None");    break;
    }

    Json::Array orig_arr;
    for (const auto &ench : sol.original_ench) {
        Json::Object eo;
        eo["id"]    = Json(ench.id.str());
        eo["level"] = Json(ench.level);
        orig_arr.push_back(Json(eo));
    }
    s["original_ench"] = Json(orig_arr);
    s["target_item"]   = reference_item_json(sol.target_item, cat_reg, eq_reg);

    Json::Array avail_arr;
    for (const auto &item : sol.available_items) {
        avail_arr.push_back(reference_item_json(item, cat_reg, eq_reg));
    }
    s["available_items"] = Json(avail_arr);

    Json::Array steps_arr;
    for (const auto &step : sol.steps) {
        Json::Object st;
        st["item_a"]         = reference_item_json(step.item_a, cat_reg, eq_reg);
        st["item_b"]         = reference_item_json(step.item_b, cat_reg, eq_reg);
        st["result"]         = reference_item_json(step.result, cat_reg, eq_reg);
        st["exp_level_cost"] = Json(step.exp_level_cost);
        st["exp_cost"]       = Json(step.exp_cost);
        steps_arr.push_back(Json(st));
    }
    s["steps"] = Json(steps_arr);

    s["total_exp_level_cost"] = Json(sol.total_exp_level_cost);
    s["total_exp_cost"]       = Json(sol.total_exp_cost);
    s["peak_level_cost"]      = Json(sol.get_peak_level_cost());
    s["peak_exp_cost"]        = Json(sol.get_peak_exp_cost());
    s["max_cost_step_index"]  = Json(static_cast<int64_t>(sol.max_cost_step_index));
    s["is_success"]           = Json(sol.is_success);

    Json::Object meta;
    meta["algorithm_name"]    = Json(sol.metadata.algorithm_name);
    meta["algorithm_version"] = Json(sol.metadata.algorithm_version);
    meta["created_at"] = Json(static_cast<int64_t>(
        sol.metadata.created_at.time_since_epoch().count()));
    meta["computation_time"] = Json(static_cast<int64_t>(
        sol.metadata.computation_time.count()));
    s["metadata"] = Json(meta);
    return Json(s);
}

// ─── Test: format_json is assembled from the ds output schema ─────────────
// 1) schema_version moved 1.0 → 1.1;
// 2) the emitted tree equals the frozen hand-built wire shape exactly;
// 3) the emitted JSON re-parses into RootView (all fields present + typed).

void test_format_json_schema_encode() {
    g_fx.init_chestplate_set();
    auto profile = profile_from_fx(g_fx);
    EnchantmentRegistry& enchants = g_fx.enchants;

    EnchSet prot3;
    const auto& prot_info = enchants.at(NSID("protection"));
    prot3.emplace(prot_info.id, prot_info.name, 3);
    EnchSet unbr2;
    const auto& unbr_info = enchants.at(NSID("unbreaking"));
    unbr2.emplace(unbr_info.id, unbr_info.name, 2);

    const auto& equip = g_fx.equipment.at(NSID("minecraft:diamond_chestplate"));

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.metadata.algorithm_name = "dp_merge";
    solution.metadata.algorithm_version = "1.2.3";
    solution.metadata.created_at =
        std::chrono::system_clock::time_point(std::chrono::seconds(1700000000));
    solution.metadata.computation_time = std::chrono::milliseconds(42);
    solution.original_ench = prot3;
    solution.target_item = Item(equip.id, EnchSet{}, 0, equip.max_durability);
    solution.available_items = {
        Item(NSID("minecraft:enchanted_book"), prot3, 0),
        Item(NSID("minecraft:enchanted_book"), unbr2, 1),
    };

    Solution::EnchStep step;
    step.exp_level_cost = 3;
    step.exp_cost       = 3;
    step.item_a = solution.target_item;
    step.item_b = Item(NSID("minecraft:enchanted_book"), prot3, 0);
    step.result = Item(equip.id, prot3, 0, equip.max_durability);
    solution.steps.push_back(step);
    solution.total_exp_level_cost = 3;
    solution.total_exp_cost       = 3;
    solution.max_cost_step_index  = 0;

    const auto json_str = OutputFormatter::format_json(
        {solution}, profile, AlgorithmMode::direct, true, "dp_merge", 42);
    Json root = Json::parse(json_str);

    // 1) schema_version bumped to 1.1
    expect(root["schema_version"].as<std::string>() == "1.1",
           "schema encode: schema_version 1.1");

    // 2) exact equivalence with the frozen hand-built wire shape
    Json expected = Json::object()
        .set("schema_version", Json("1.1"))
        .set("mode", Json("direct"))
        .set("success", Json(true))
        .set("algorithm", Json("dp_merge"))
        .set("computation_time_ms", Json(int64_t(42)))
        .set("solutions", Json(Json::Array{reference_solution_json(
                  solution, 1, profile.tags(), profile.eq())}));
    expect(root == expected, "schema encode: equals hand-built wire shape");

    // 3) structural validation: the emitted JSON parses into RootView
    RootView decoded;
    ds::ErrorList err;
    bool ok = ds::json::Schema<RootSchema>::parse(root, decoded, err);
    expect(ok, "schema encode: root validates against RootSchema");
    expect(err.empty(), "schema encode: parse errors: " + err.str());

    // Spot-check that values flow through the schema, not just key presence.
    expect_eq(decoded.solutions.size(), 1u, "schema decode: one solution");
    if (decoded.solutions.empty())
        return;  // avoid crashing on a failed parse below
    const auto& d = decoded.solutions[0];
    expect_eq(d.rank, 1, "schema decode: rank");
    expect(d.platform == "Java", "schema decode: platform");
    expect_eq(d.total_exp_level_cost, 3, "schema decode: total_exp_level_cost");
    expect_eq(d.peak_level_cost, 3, "schema decode: peak_level_cost");
    expect(d.is_success, "schema decode: is_success");
    expect(d.metadata.algorithm_name == "dp_merge",
           "schema decode: metadata.algorithm_name");
    expect_eq(d.metadata.computation_time, 42, "schema decode: metadata.computation_time");
    expect_eq(d.original_ench.size(), 1u, "schema decode: original_ench");
    expect_eq(d.available_items.size(), 2u, "schema decode: available_items");
    expect_eq(d.steps.size(), 1u, "schema decode: steps");
    const auto& d_step = d.steps[0];
    expect_eq(d_step.exp_level_cost, 3, "schema decode: step exp_level_cost");
    expect(d_step.item_b.is_book, "schema decode: step item_b is a book");
    expect(!d_step.item_b.equipment.has_value(), "schema decode: book equipment null");
    expect(!d_step.result.is_book, "schema decode: step result is equipment");
    expect(d_step.result.equipment.has_value(), "schema decode: result has equipment");
    expect(d_step.result.equipment->id == "minecraft:diamond_chestplate",
           "schema decode: result equipment id");
    expect_eq(d_step.result.enchantments.size(), 1u,
              "schema decode: result enchantments");
    expect(d.target_item.equipment.has_value(), "schema decode: target equipment");
    expect_eq(d.target_item.equipment->max_durability, 528,
              "schema decode: target max_durability");
    expect(d.target_item.equipment->category == "chestplate",
           "schema decode: target category");

    TEST_PASS("format_json ds schema encode");
}

// ─── Test: build_json_root (C ABI shared root) is schema-driven ──────────

void test_build_json_root_schema() {
    Json root = OutputFormatter::build_json_root(
        AlgorithmMode::inventory, false, "bb_dp", 1234);

    RootMetaView decoded;
    ds::ErrorList err;
    bool ok = ds::json::Schema<RootMetaSchema>::parse(root, decoded, err);
    expect(ok, "build_json_root: validates against RootMetaSchema");
    expect(err.empty(), "build_json_root: parse errors: " + err.str());
    expect(decoded.schema_version == "1.1", "build_json_root: schema_version 1.1");
    expect(decoded.mode == "inventory", "build_json_root: mode");
    expect(!decoded.success, "build_json_root: success");
    expect(decoded.algorithm == "bb_dp", "build_json_root: algorithm");
    expect_eq(decoded.computation_time_ms, 1234, "build_json_root: computation_time_ms");

    TEST_PASS("build_json_root ds schema");
}

} // anonymous namespace

int main() {
    try {
        g_fx.init_sword_set();  // Initialize default registries

        test_format_book_solution();
        test_format_combined_solution();
        test_format_no_steps();
        test_format_unsuccessful();
        test_format_multi_step();
        test_verbose_item_format();
        test_format_verbose_final_item();
        test_format_verbose_too_expensive();
        test_format_json_real_equipment();
        test_format_json_step_result();
        test_format_compact_platform_raw();
        test_json_roundtrip();
        test_format_json_schema_encode();
        test_build_json_root_schema();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
