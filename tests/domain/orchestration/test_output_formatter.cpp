#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include <iostream>
#include <string>
#include <vector>

static TestFixture g_fx;

namespace {

// ─── Test 1: format a simple book solution ─────────────────────────

void test_format_book_solution() {
    g_fx.init_sword_set();

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.total_exp_level_cost = 5;
    solution.total_exp_cost = 5;

    Solution::EnchStep step;
    step.exp_level_cost = 5;
    step.exp_cost = 5;
    step.item_a = Item(EnchSet{}, 0);
    step.item_b = Item(EnchSet{}, 0);

    // Need to construct a valid solution with target equipment
    solution.steps.push_back(step);
    solution.target_item = Item(
        g_fx.equipment.get("diamond_sword"),
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format(solution);
    expect(!formatted.empty(), "format: should produce non-empty output");

    std::cout << "PASS: test_format_book_solution" << std::endl;
}

// ─── Test 2: format a combined solution with raw adapter ────────────

void test_format_combined_solution() {
    g_fx.init_chestplate_set();
    EnchantmentRegistry& enchants = g_fx.enchants;

    // Create a protection 3 book
    EnchSet enchants_set;
    int32_t prot_id = enchants.get_id("protection");
    enchants_set.emplace(static_cast<int32_t>(prot_id), 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.target_item = Item(
        g_fx.equipment.get("diamond_chestplate"),
        EnchSet{}, 0, 528
    );

    Solution::EnchStep step;
    step.exp_level_cost = 3;
    step.exp_cost = 3;
    step.item_a = solution.target_item;
    step.item_b = Item(enchants_set, 0);
    solution.steps.push_back(step);
    solution.total_exp_level_cost = 3;
    solution.total_exp_cost = 3;

    auto formatted = OutputFormatter::format(solution);
    expect(!formatted.empty(), "format_combined: should produce non-empty output");

    std::cout << "PASS: test_format_combined_solution" << std::endl;
}

// ─── Test 3: format with no steps (edge case) ──────────────────────

void test_format_no_steps() {
    g_fx.init_sword_set();

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.target_item = Item(
        g_fx.equipment.get("diamond_sword"),
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format(solution);
    expect(!formatted.empty(), "format_no_steps: should still produce output");

    std::cout << "PASS: test_format_no_steps" << std::endl;
}

// ─── Test 4: format with unsuccesful solution (zero steps) ─────────

void test_format_unsuccessful() {
    g_fx.init_sword_set();

    Solution solution;
    solution.is_success = false;
    solution.platform = MCE::Java;
    solution.target_item = Item(
        g_fx.equipment.get("diamond_sword"),
        EnchSet{}, 0, 1561
    );

    auto formatted = OutputFormatter::format(solution);
    expect(!formatted.empty(), "format_unsuccessful: should still produce output");

    std::cout << "PASS: test_format_unsuccessful" << std::endl;
}

// ─── Test 5: format with multiple steps ─────────────────────────────

void test_format_multi_step() {
    g_fx.init_chestplate_set();
    EnchantmentRegistry& enchants = g_fx.enchants;

    EnchSet prot3;
    int32_t prot_id = enchants.get_id("protection");
    prot3.emplace(static_cast<int32_t>(prot_id), 3);

    EnchSet unbr3;
    int32_t unbr_id = enchants.get_id("unbreaking");
    unbr3.emplace(static_cast<int32_t>(unbr_id), 3);

    Solution solution;
    solution.is_success = true;
    solution.platform = MCE::Java;
    solution.target_item = Item(
        g_fx.equipment.get("diamond_chestplate"),
        EnchSet{}, 0, 528
    );

    // Step 1: add protection 3
    Solution::EnchStep step1;
    step1.exp_level_cost = 3;
    step1.exp_cost = 3;
    step1.item_a = solution.target_item;
    step1.item_b = Item(prot3, 0);
    solution.steps.push_back(step1);

    // Step 2: add unbreaking 3
    Solution::EnchStep step2;
    step2.exp_level_cost = 3;
    step2.exp_cost = 3;
    step2.item_a = step1.item_a;
    step2.item_b = Item(unbr3, 0);
    solution.steps.push_back(step2);

    solution.total_exp_level_cost = 6;
    solution.total_exp_cost = 6;

    auto formatted = OutputFormatter::format(solution);
    expect(!formatted.empty(), "format_multi: should produce output");

    // Multi-step output should have more lines than single-step
    auto single_out = OutputFormatter::format([]{
        Solution s;
        s.is_success = true;
        s.platform = MCE::Java;
        s.target_item = Item(
            g_fx.equipment.get("diamond_chestplate"),
            EnchSet{}, 0, 528
        );
        EnchSet ench;
        ench.emplace(static_cast<int32_t>(g_fx.enchants.get_id("protection")), 3);
        Solution::EnchStep st;
        st.exp_level_cost = 3;
        st.exp_cost = 3;
        st.item_a = s.target_item;
        st.item_b = Item(ench, 0);
        s.steps.push_back(st);
        s.total_exp_level_cost = 3;
        s.total_exp_cost = 3;
        return s;
    }());
    expect(formatted.size() > single_out.size(),
           "format_multi: multi-step output should be larger than single-step");

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
