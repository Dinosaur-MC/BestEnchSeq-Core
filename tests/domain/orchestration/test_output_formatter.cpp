#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/orchestration/orchestration.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include <iostream>
#include <string>
#include <vector>

static TestFixture g_fx;

namespace {

// Helper: build a Profile from the TestFixture's registries
Profile profile_from_fx(const TestFixture& fx) {
    Profile profile(NSID("test:formatter"));
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
    expect(output.find("(free)") != std::string::npos,
           "verbose output should show (free) for empty items with ppn=0");
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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
