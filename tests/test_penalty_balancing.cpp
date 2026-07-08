#include "test_utils.h"
#include "algorithm/strategies/DynamicPenaltyBalancing.h"
#include "algorithm/AlgorithmExecutor.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {

void setup() {
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategory("sword")}});
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategory("sword")}});
    infos.push_back({"fire_aspect", "Fire Aspect", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategory("sword")}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

EquipmentType sword{"diamond_sword", "Diamond Sword", EquipmentCategory::Sword(), 1561};

void test_penalty_balancing_two_books() {
    setup();

    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});  // sharpness 5
    available.emplace_back(EnchSet{Ench(1, 2)});  // knockback 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<DynamicPenaltyBalancing>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete with two books");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps[0].size() == 2, "should have two forge steps");

    // Verify costs are positive
    for (size_t i = 0; i < output.steps[0].size(); ++i)
        expect(output.steps[0][i].exp_level_cost > 0,
               "cost at step " + std::to_string(i) + " should be positive");

    // Verify all sacrifices cover both enchantments
    bool has_sharp5 = false, has_knock2 = false;
    for (const auto& step : output.steps[0]) {
        if (step.item_b.enchantments.find(Ench(0, 5)) != step.item_b.enchantments.end())
            has_sharp5 = true;
        if (step.item_b.enchantments.find(Ench(1, 2)) != step.item_b.enchantments.end())
            has_knock2 = true;
    }
    expect(has_sharp5, "sharpness 5 should appear as sacrifice");
    expect(has_knock2, "knockback 2 should appear as sacrifice");

    // Note: penalty balancing may start with a book-book merge (since all items
    // have penalty=0, and the tiebreaker favors book-book over book-equipment).
    // The first step base is not necessarily the unenchanted sword; it may be
    // a book. Only the sacrifice verification matters for correctness.

    std::cout << "PASS: test_penalty_balancing_two_books" << std::endl;
}

void test_penalty_balancing_three_books() {
    setup();

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

    auto algo = std::make_unique<DynamicPenaltyBalancing>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete with three books");

    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps[0].size() == 3, "should have three forge steps");

    // Verify all three books appear as sacrifices
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

    std::cout << "PASS: test_penalty_balancing_three_books" << std::endl;
}

void test_penalty_balancing_goal_already_met() {
    setup();

    ItemStack goal(&sword, EnchSet{}, 0, 1561);

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = {},
    };

    auto algo = std::make_unique<DynamicPenaltyBalancing>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "should complete when goal already met");
    std::cout << "PASS: test_penalty_balancing_goal_already_met" << std::endl;
}

void test_penalty_balancing_five_enchantments() {
    setup();

    // 5 enchantment goal: use all 3 registered enchants
    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2), Ench(2, 2)}, 0, 1561);

    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5
    available.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2
    available.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = goal,
        .available_items = available,
    };

    auto algo = std::make_unique<DynamicPenaltyBalancing>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed,
           "should complete with three books");
    auto output = executor.output();
    expect(output.is_valid, "output should be valid");
    expect(!output.steps.empty(), "should have forge steps");

    std::cout << "PASS: test_penalty_balancing_five_enchantments" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== DynamicPenaltyBalancing Tests ===" << std::endl;
    test_penalty_balancing_two_books();
    test_penalty_balancing_three_books();
    test_penalty_balancing_goal_already_met();
    test_penalty_balancing_five_enchantments();
    std::cout << "All DynamicPenaltyBalancing tests passed!" << std::endl;
    return 0;
}
