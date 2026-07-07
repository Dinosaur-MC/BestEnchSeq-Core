#include "test_utils.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
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
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

EquipmentType sword{"diamond_sword", "Diamond Sword", EquipmentCategory::Sword(), 1561};

void test_greedy_forges_items() {
    setup();

    // Target: unenchanted sword
    ItemStack target(&sword, EnchSet{}, 0, 1561);

    // Available: 2 books
    ItemCollection available;
    available.emplace_back(EnchSet{Ench(0, 5)});  // sharpness 5
    available.emplace_back(EnchSet{Ench(1, 2)});  // knockback 2

    AlgorithmInput input{
        .platform = platform::MCE::Java,
        .original_ench = EnchSet{},
        .target_item = target,
        .available_items = available,
    };

    auto algo = std::make_unique<GreedyAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");

    // Verify output structure
    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps.size() == 1, "should have one solution");
    expect(output.steps[0].size() == 2, "solution should have two forge steps");

    // Verify costs are positive and match expected values
    // Step 1: empty sword + Sharpness 5 book -> cost = multiplier(1)*5 = 5
    expect(output.steps[0][0].exp_level_cost == 5,
           "first step cost should be 5 for sharpness 5 on empty sword");
    expect(output.steps[0][0].exp_level_cost > 0, "forge cost should be positive");

    // Step 2: sword(Sharpness 5) + Knockback 2 book -> cost = multiplier(1)*2 + penalty(1) = 3
    expect(output.steps[0][1].exp_level_cost == 3,
           "second step cost should be 3 for knockback 2 with prior penalty");
    expect(output.steps[0][1].exp_level_cost > 0, "forge cost should be positive");

    // Verify enchantments on intermediate and final items
    // First step item_a (base) should have no enchantments
    expect(output.steps[0][0].item_a.enchantments.find(Ench(0, 5))
           == output.steps[0][0].item_a.enchantments.end(),
           "first step base should not have sharpness");
    // First step item_b should have sharpness
    expect(output.steps[0][0].item_b.enchantments.find(Ench(0, 5))
           != output.steps[0][0].item_b.enchantments.end(),
           "first step sacrifice should have sharpness");

    // Second step item_a (result after first forge) should have sharpness
    expect(output.steps[0][1].item_a.enchantments.find(Ench(0, 5))
           != output.steps[0][1].item_a.enchantments.end(),
           "second step base should have sharpness after first forge");
    // Second step item_b should have knockback
    expect(output.steps[0][1].item_b.enchantments.find(Ench(1, 2))
           != output.steps[0][1].item_b.enchantments.end(),
           "second step sacrifice should have knockback");
    std::cout << "PASS: test_greedy_forges_items" << std::endl;
}
} // namespace

int main() {
    test_greedy_forges_items();
    std::cout << "All greedy algorithm tests passed!" << std::endl;
    return 0;
}
