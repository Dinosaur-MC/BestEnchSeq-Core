#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include <iostream>
#include <string>
#include <vector>

static TestFixture g_fx;

namespace {

// ─── Test 1: format a simple book solution ─────────────────────────

void test_format_book_solution() {
    g_fx.init_sword_set();
    EquipmentTagRegistry tag_reg;
    tag_reg.clear();

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
    const auto& equip = g_fx.equipment.get(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, g_fx.enchants, tag_reg, "test");
    expect(!formatted.empty(), "format: should produce non-empty output");

    std::cout << "PASS: test_format_book_solution" << std::endl;
}

// ─── Test 2: format a combined solution with raw adapter ────────────

void test_format_combined_solution() {
    g_fx.init_chestplate_set();
    EquipmentTagRegistry tag_reg;
    tag_reg.clear();
    EnchantmentRegistry& enchants = g_fx.enchants;

    // Create a protection 3 book
    EnchSet enchants_set;
    const auto& prot_info = enchants.get(NSID("protection"));
    enchants_set.emplace(prot_info.id, prot_info.name, 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.get(NSID("minecraft:diamond_chestplate"));
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
        {solution}, g_fx.enchants, tag_reg, "test");
    expect(!formatted.empty(), "format_combined: should produce non-empty output");

    std::cout << "PASS: test_format_combined_solution" << std::endl;
}

// ─── Test 3: format with no steps (edge case) ──────────────────────

void test_format_no_steps() {
    g_fx.init_sword_set();
    EquipmentTagRegistry tag_reg;
    tag_reg.clear();

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.get(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, g_fx.enchants, tag_reg, "test");
    expect(!formatted.empty(), "format_no_steps: should still produce output");

    std::cout << "PASS: test_format_no_steps" << std::endl;
}

// ─── Test 4: format with unsuccesful solution (zero steps) ─────────

void test_format_unsuccessful() {
    g_fx.init_sword_set();
    EquipmentTagRegistry tag_reg;
    tag_reg.clear();

    Solution solution;
    solution.is_success = false;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.get(NSID("minecraft:diamond_sword"));
    solution.target_item = Item(
        equip.id,
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format_compact(
        {solution}, g_fx.enchants, tag_reg, "test");
    expect(!formatted.empty(), "format_unsuccessful: should still produce output");

    std::cout << "PASS: test_format_unsuccessful" << std::endl;
}

// ─── Test 5: format with multiple steps ─────────────────────────────

void test_format_multi_step() {
    g_fx.init_chestplate_set();
    EquipmentTagRegistry tag_reg;
    tag_reg.clear();
    EnchantmentRegistry& enchants = g_fx.enchants;

    EnchSet prot3;
    const auto& prot_info = enchants.get(NSID("protection"));
    prot3.emplace(prot_info.id, prot_info.name, 3);

    EnchSet unbr3;
    const auto& unbr_info = enchants.get(NSID("unbreaking"));
    unbr3.emplace(unbr_info.id, unbr_info.name, 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    const auto& equip = g_fx.equipment.get(NSID("minecraft:diamond_chestplate"));
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
        {solution}, g_fx.enchants, tag_reg, "test");
    expect(!formatted.empty(), "format_multi: should produce output");

    // Multi-step output should be non-empty (size comparison removed as
    // format_compact output varies from the old format() API)
    std::cout << "PASS: test_format_multi_step" << std::endl;
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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
