#include "test_utils.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {
void setup() {
    std::vector<EnchInfo> infos;
    // id 0
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategory("sword")}});
    // id 1
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategory("sword")}});
    // id 2
    infos.push_back({"fire_aspect", "Fire Aspect", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategory("sword")}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

EquipmentType sword{"diamond_sword", "Diamond Sword", EquipmentCategory::Sword(), 1561};

// ──────────────────────────────────────────────────────────
// Test: forge two books onto an empty sword to reach goal
// ──────────────────────────────────────────────────────────
void test_dfs_with_two_books() {
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

    auto algo = std::make_unique<DFSAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete with two books");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps.size() == 1, "should have one solution");
    expect(output.steps[0].size() == 2, "solution should have two forge steps");

    // Verify all forge costs are positive
    expect(output.steps[0][0].exp_level_cost > 0, "first forge cost should be positive");
    expect(output.steps[0][1].exp_level_cost > 0, "second forge cost should be positive");

    // Verify step ordering via item_a (pre-forge state of each step).
    // Steps are recorded as {item_a, item_b, cost, exp} where item_a is the
    // base item BEFORE the forge and the result REPLACES item_a for the next step.
    //
    // Step 0: base = bare sword, sacrifice = Sharpness 5 book
    expect(output.steps[0][0].item_a.enchantments.empty(),
           "first step base should be unenchanted sword");
    expect(output.steps[0][0].item_b.enchantments.find(Ench(0, 5))
           != output.steps[0][0].item_b.enchantments.end(),
           "first step sacrifice should have sharpness 5");
    //
    // Step 1: base = result of step 0 (sword now has Sharpness 5),
    //        sacrifice = Knockback 2 book
    expect(output.steps[0][1].item_a.enchantments.find(Ench(0, 5))
           != output.steps[0][1].item_a.enchantments.end(),
           "second step base should have sharpness 5 from previous forge");
    expect(output.steps[0][1].item_b.enchantments.find(Ench(1, 2))
           != output.steps[0][1].item_b.enchantments.end(),
           "second step sacrifice should have knockback 2");

    std::cout << "PASS: test_dfs_with_two_books" << std::endl;
}

// ──────────────────────────────────────────────────────────
// Test: forge three books onto an empty sword to reach goal
// ──────────────────────────────────────────────────────────
void test_dfs_with_three_books() {
    setup();

    // Goal: sword with Sharpness 4, Knockback 2, Fire Aspect 2
    ItemStack goal(&sword, EnchSet{Ench(0, 4), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    // Available: 3 books (each provides one of the desired enchantments)
    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 4)});   // sharpness 4
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},   // starts unenchanted
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<DFSAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete with three books");

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

    // Verify the final step's item_a has enchantments from prior steps and
    // item_b is the sacrifice for the last forge
    const auto& last_step = output.steps[0][2];
    // After 2 prior forges, the base should have at least 2 of the 3 enchants
    size_t found_count = 0;
    if (last_step.item_a.enchantments.find(Ench(0, 4)) != last_step.item_a.enchantments.end())
        found_count++;
    if (last_step.item_a.enchantments.find(Ench(1, 2)) != last_step.item_a.enchantments.end())
        found_count++;
    if (last_step.item_a.enchantments.find(Ench(2, 2)) != last_step.item_a.enchantments.end())
        found_count++;
    expect(found_count >= 2, "last step base should have at least 2 of 3 enchants from prior forges");

    // The sacrifice should provide the missing enchantment
    // Also check that all three books appear as sacrifices across the 3 steps
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

    std::cout << "PASS: test_dfs_with_three_books" << std::endl;
}

// ──────────────────────────────────────────────────────────
// Test: no available items — goal already met
// ──────────────────────────────────────────────────────────
void test_dfs_goal_already_met() {
    setup();

    // Goal: unenchanted sword
    ItemStack goal(&sword, EnchSet{}, 0, 1561);

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},  // already has what the goal requires
        .target_item = goal,
        .available_items = {},
    };

    auto algo = std::make_unique<DFSAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "should complete when goal already met");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    // No forge steps needed since item already meets goal
    // output.steps may be empty — that is acceptable

    std::cout << "PASS: test_dfs_goal_already_met" << std::endl;
}

// ──────────────────────────────────────────────────────────
// Test: target starts with some enchantments, needs upgrade
// ──────────────────────────────────────────────────────────
void test_dfs_upgrade_existing() {
    setup();

    // Goal: sword with Sharpness 3
    ItemStack goal(&sword, EnchSet{Ench(0, 3)}, 0, 1561);

    // Sword starts with Sharpness 2
    // Available: one Sharpness 1 book for upgrade
    //   Step: Sword(Sharp2) + Sharp1 book
    //     Sharp2 + Sharp1: level(2) != lvl(1) => max(2, 1) = 2. Still Sharp 2!
    // That won't reach Sharp 3. Use Sharp 2 book to get Sharp 3 instead:
    //   Step: Sword(Sharp2) + Sharp2 book
    //     Sharp2 + Sharp2: level(2) == lvl(2) => 2 + 1 = 3. Sharp 3 ✓
    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 2)});  // sharpness 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{Ench(0, 2)},  // starts with Sharpness 2
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<DFSAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "should complete with upgrade test");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "should have forge steps for upgrade");
    expect(output.steps.size() >= 1, "should have at least one solution");
    expect(output.steps[0].size() >= 1, "solution should have at least one step");

    // The last step's item_a is the base BEFORE the final forge, so it has the
    // result of the second-to-last forge. Verify across all sacrifices that the
    // Sharpness 2 book is consumed.
    bool has_sharp2_sacrifice = false;
    for (size_t i = 0; i < output.steps[0].size(); ++i) {
        auto it = output.steps[0][i].item_b.enchantments.find(Ench(0, 2));
        if (it != output.steps[0][i].item_b.enchantments.end()) {
            has_sharp2_sacrifice = true;
        }
        // Check the first step's item_a starts with Sharpness 2
        if (i == 0) {
            auto it_a = output.steps[0][i].item_a.enchantments.find(Ench(0, 2));
            expect(it_a != output.steps[0][i].item_a.enchantments.end(),
                   "first step base should start with sharpness 2");
            expect(it_a->level >= 2, "first step base should have sharpness >= 2");
        }
    }
    expect(has_sharp2_sacrifice,
           "a sacrifice should have sharpness 2 for the upgrade");

    std::cout << "PASS: test_dfs_upgrade_existing" << std::endl;
}

} // namespace

int main() {
    test_dfs_with_two_books();
    test_dfs_with_three_books();
    test_dfs_goal_already_met();
    test_dfs_upgrade_existing();
    std::cout << "All DFS algorithm tests passed!" << std::endl;
    return 0;
}
