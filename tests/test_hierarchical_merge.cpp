#include "test_utils.h"
#include "algorithm/strategies/HierarchicalMergeStrategy.h"
#include "algorithm/AlgorithmExecutor.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {

void setup() {
    std::vector<EnchInfo> infos;
    // id 0 - mult 1
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    // id 1 - mult 2
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    // id 2 - mult 2
    infos.push_back({"fire_aspect", "Fire Aspect", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

void test_hierarchical_two_books() {
    setup();

    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});  // sharpness 5 (mult 1 → low group)
    available.emplace_back(EnchSet{Ench(1, 2)});  // knockback 2 (mult 2 → mid group)

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<HierarchicalMergeStrategy>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");
    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "should have steps");

    // Verify both enchantments are present
    bool has_sharp5 = false, has_knock2 = false;
    for (const auto& step : output.steps[0]) {
        if (step.item_b.enchantments.find(Ench(0, 5)) != step.item_b.enchantments.end())
            has_sharp5 = true;
        if (step.item_b.enchantments.find(Ench(1, 2)) != step.item_b.enchantments.end())
            has_knock2 = true;
    }
    expect(has_sharp5, "sharpness 5 should appear as sacrifice");
    expect(has_knock2, "knockback 2 should appear as sacrifice");

    std::cout << "PASS: test_hierarchical_two_books" << std::endl;
}

void test_hierarchical_three_books() {
    setup();

    ItemStack goal(&sword, EnchSet{Ench(0, 4), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 4)});   // sharpness 4 (mult 1)
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2 (mult 2)
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2 (mult 2)

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<HierarchicalMergeStrategy>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");
    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "should have steps");
    expect(output.steps[0].size() == 3, "should have three steps");

    // Verify all three sacrifices
    bool has_sharp = false, has_knock = false, has_fire = false;
    for (const auto& step : output.steps[0]) {
        if (step.item_b.enchantments.find(Ench(0, 4)) != step.item_b.enchantments.end())
            has_sharp = true;
        if (step.item_b.enchantments.find(Ench(1, 2)) != step.item_b.enchantments.end())
            has_knock = true;
        if (step.item_b.enchantments.find(Ench(2, 2)) != step.item_b.enchantments.end())
            has_fire = true;
    }
    expect(has_sharp, "sharpness 4 should appear as sacrifice");
    expect(has_knock, "knockback 2 should appear as sacrifice");
    expect(has_fire, "fire_aspect 2 should appear as sacrifice");

    std::cout << "PASS: test_hierarchical_three_books" << std::endl;
}

void test_hierarchical_goal_already_met() {
    setup();

    ItemStack goal(&sword, EnchSet{}, 0, 1561);

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = {},
    };

    auto algo = std::make_unique<HierarchicalMergeStrategy>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "should complete when goal already met");
    std::cout << "PASS: test_hierarchical_goal_already_met" << std::endl;
}

void test_hierarchical_seven_books() {
    setup();

    // Need more enchantment types; reuse existing ones with variations
    // Sharp5, Sharp5, Sharp5, Sharp5, Sharp5, Knock2, Fire2
    // Multiple Sharp5 books should get deduped in Phase 1
    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5 (dup)
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5 (dup)
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5 (dup)
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5 (dup)
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<HierarchicalMergeStrategy>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);

    auto state = executor.wait_for(std::chrono::seconds(10));
    expect(state == AlgorithmState::Completed,
           "should complete with seven books (state: " + std::to_string(static_cast<int>(state)) + ")");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "should have steps");

    std::cout << "PASS: test_hierarchical_seven_books" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== HierarchicalMergeStrategy Tests ===" << std::endl;
    test_hierarchical_two_books();
    test_hierarchical_three_books();
    test_hierarchical_goal_already_met();
    test_hierarchical_seven_books();
    std::cout << "All HierarchicalMergeStrategy tests passed!" << std::endl;
    return 0;
}
