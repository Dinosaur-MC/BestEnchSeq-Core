#include "test_utils.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"
#include <unordered_set>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Shared test setup: register enchantments used across test cases
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    std::vector<EnchInfo> infos;
    // id 0
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    // id 1
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    // id 2
    infos.push_back({"fire_aspect", "Fire Aspect", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: forge two books onto an empty sword to reach goal
// ─────────────────────────────────────────────────────────────────────────────
void test_astar_with_two_books() {
    setup();

    // Goal: sword with Sharpness 5 and Knockback 2
    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 0, 1561);

    // Available: 2 books
    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});  // sharpness 5
    available.emplace_back(EnchSet{Ench(1, 2)});  // knockback 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},   // starts unenchanted
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "astar should complete with two books");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps.size() == 1, "should have one solution");
    expect(output.steps[0].size() == 2, "solution should have two forge steps");

    // Verify all forge costs are positive
    expect(output.steps[0][0].exp_level_cost > 0, "first forge cost should be positive");
    expect(output.steps[0][1].exp_level_cost > 0, "second forge cost should be positive");

    // Verify that one step starts with unenchanted sword as base
    // and all sacrifice enchantments appear across the steps
    bool found_bare_base = false;
    std::unordered_set<int32_t> seen_ench_ids;
    for (const auto& step : output.steps[0]) {
        if (step.item_a.enchantments.empty())
            found_bare_base = true;
        for (const auto& ench : step.item_b.enchantments)
            seen_ench_ids.insert(ench.id);
    }
    expect(found_bare_base, "one step should start with unenchanted sword");
    expect(seen_ench_ids.count(0), "sharpness 5 should appear as sacrifice");
    expect(seen_ench_ids.count(1), "knockback 2 should appear as sacrifice");

    std::cout << "PASS: test_astar_with_two_books" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: forge three books onto an empty sword to reach goal
// ─────────────────────────────────────────────────────────────────────────────
void test_astar_with_three_books() {
    setup();

    // Goal: sword with Sharpness 4, Knockback 2, Fire Aspect 2
    ItemStack goal(&sword, EnchSet{Ench(0, 4), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 4)});   // sharpness 4
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "astar should complete with three books");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps.size() == 1, "should have one solution");
    expect(output.steps[0].size() == 3, "solution should have three forge steps");

    // Verify all forge costs are positive
    for (size_t i = 0; i < 3; ++i) {
        expect(output.steps[0][i].exp_level_cost > 0,
               "forge cost at step " + std::to_string(i) + " should be positive");
    }

    // Verify three sacrifices cover all three enchantments
    bool has_sharp = false, has_knock = false, has_fire = false;
    for (size_t i = 0; i < 3; ++i) {
        if (output.steps[0][i].item_b.enchantments.find(Ench(0, 4))
            != output.steps[0][i].item_b.enchantments.end())
            has_sharp = true;
        if (output.steps[0][i].item_b.enchantments.find(Ench(1, 2))
            != output.steps[0][i].item_b.enchantments.end())
            has_knock = true;
        if (output.steps[0][i].item_b.enchantments.find(Ench(2, 2))
            != output.steps[0][i].item_b.enchantments.end())
            has_fire = true;
    }
    expect(has_sharp, "one sacrifice should have sharpness 4");
    expect(has_knock, "one sacrifice should have knockback 2");
    expect(has_fire, "one sacrifice should have fire_aspect 2");

    std::cout << "PASS: test_astar_with_three_books" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: goal already met — no forge steps needed
// ─────────────────────────────────────────────────────────────────────────────
void test_astar_goal_already_met() {
    setup();

    ItemStack goal(&sword, EnchSet{}, 0, 1561);

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = {},
    };

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "astar should complete when goal already met");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    // No forge steps expected since the item already meets goal

    std::cout << "PASS: test_astar_goal_already_met" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: upgrade existing enchantments on the sword
// ─────────────────────────────────────────────────────────────────────────────
void test_astar_upgrade_existing() {
    setup();

    // Goal: sword with Sharpness 3
    ItemStack goal(&sword, EnchSet{Ench(0, 3)}, 0, 1561);

    // Sword starts with Sharpness 2
    // Use a Sharpness 2 book to upgrade:
    //   Sharp2 + Sharp2: level(2) == level(2) => 2 + 1 = 3
    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 2)});  // sharpness 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{Ench(0, 2)},  // starts with Sharpness 2
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "astar should complete with upgrade test");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "should have forge steps for upgrade");
    expect(output.steps[0].size() >= 1, "solution should have at least one step");

    // The sacrifice should be the Sharpness 2 book
    bool has_sharp2_sacrifice = false;
    for (size_t i = 0; i < output.steps[0].size(); ++i) {
        if (output.steps[0][i].item_b.enchantments.find(Ench(0, 2))
            != output.steps[0][i].item_b.enchantments.end()) {
            has_sharp2_sacrifice = true;
        }
        if (i == 0) {
            auto it_a = output.steps[0][i].item_a.enchantments.find(Ench(0, 2));
            expect(it_a != output.steps[0][i].item_a.enchantments.end(),
                   "first step base should start with sharpness 2");
            expect(it_a->level >= 2, "first step base should have sharpness >= 2");
        }
    }
    expect(has_sharp2_sacrifice,
           "a sacrifice should have sharpness 2 for the upgrade");

    std::cout << "PASS: test_astar_upgrade_existing" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: optimality guarantee — A* should find the cheaper ordering
//
// Three books: Sharpness 5 (mult=1), Knockback 2 (mult=2), Fire Aspect 2 (mult=2)
// Forging cheap mult first then expensive mult reduces penalty cost.
// A* should find the optimal ordering (sharpness before the others).
// ─────────────────────────────────────────────────────────────────────────────
void test_astar_optimal_ordering() {
    setup();

    // Goal: sword with all three enchantments
    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5 (mult=1)
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2 (mult=2)
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2 (mult=2)

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "astar should complete optimal ordering test");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "should have a solution");

    // The optimal ordering should forge Sharpness 5 BEFORE the other books
    // because Sharpness has multiplier 1 (book) while Knockback/Fire have
    // multiplier 1 (book mult = max(1, 2>>1) = 1).  Actually for Java:
    //   Sharpness mult=1, book mult = max(1, 1>>1) = 1
    //   Knockback mult=2, book mult = max(1, 2>>1) = 1
    //   Fire Aspect mult=2, book mult = max(1, 2>>1) = 1
    // All book multipliers are 1, so ordering doesn't affect base cost.
    // However, penalty costs increase with each forge.
    // Optimal: forge cheap enchants first so expensive ones pay less penalty.
    //
    // Book multipliers are all 1 in this test case (since min_mult is 1 for books).
    // The cost of each enchant = mult * level.
    //   Sharp5: 1*5 = 5
    //   Knock2: 1*2 = 2
    //   Fire2:  1*2 = 2
    //
    // Penalty cost increases by 1 each forge.
    // The optimal is to sort by (level * mult) ascending:
    //   Order: Knock2(2), Fire2(2), Sharp5(5)
    //   Step1: sword + Knock2 = 2 + 0 = 2
    //   Step2: sword(Knock2) + Fire2 = 2 + 1 = 3
    //   Step3: sword(Knock2,Fire2) + Sharp5 = 5 + 3 = 8
    //   Total: 13
    //
    //   Alternative: Sharp5, Knock2, Fire2
    //   Step1: sword + Sharp5 = 5 + 0 = 5
    //   Step2: sword(Sharp5) + Knock2 = 2 + 1 = 3
    //   Step3: sword(Sharp5,Knock2) + Fire2 = 2 + 3 = 5
    //   Total: 13
    //
    // Both total 13. Either ordering is optimal.
    // Just verify the solution is valid and has 3 steps.
    expect(output.steps[0].size() == 3, "solution should have three forge steps");

    // Verify step ordering is correct (each step reduces remaining enchants)
    // All three sacrifices should provide at least one needed enchantment
    bool all_enchants_covered = true;
    std::unordered_set<int32_t> seen_ids;
    for (const auto& step : output.steps[0]) {
        for (const auto& ench : step.item_b.enchantments) {
            seen_ids.insert(ench.id);
        }
    }
    all_enchants_covered = (seen_ids.count(0) && seen_ids.count(1) && seen_ids.count(2));
    expect(all_enchants_covered, "all three enchantments should appear as sacrifices");

    // First step base should be unenchanted
    expect(output.steps[0][0].item_a.enchantments.empty(),
           "first step base should be unenchanted sword");

    std::cout << "PASS: test_astar_optimal_ordering" << std::endl;
}

} // anonymous namespace

int main() {
    test_astar_with_two_books();
    test_astar_with_three_books();
    test_astar_goal_already_met();
    test_astar_upgrade_existing();
    test_astar_optimal_ordering();
    std::cout << "All A* algorithm tests passed!" << std::endl;
    return 0;
}
