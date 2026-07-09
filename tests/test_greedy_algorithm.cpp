#include "test_utils.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {
void setup() {
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

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

    auto output = executor.output();
    expect(output.is_valid, "output should be valid after completion");
    expect(!output.steps.empty(), "output steps should not be empty");
    expect(output.steps.size() == 1, "should have one solution");
    expect(output.steps[0].size() == 2, "solution should have two forge steps");

    // Verify costs are positive
    expect(output.steps[0][0].exp_level_cost > 0, "first forge cost should be positive");
    expect(output.steps[0][1].exp_level_cost > 0, "second forge cost should be positive");

    // Total cost should be 8 regardless of order
    int32_t total = output.steps[0][0].exp_level_cost + output.steps[0][1].exp_level_cost;
    expect(total == 8, "total cost should be 8");

    // Verify first step base is the unenchanted sword
    expect(output.steps[0][0].item_a.enchantments.empty(),
           "first step base should be unenchanted sword");

    // Both books must appear as sacrifices (ordering-agnostic)
    bool has_sharp5 = false, has_knock2 = false;
    for (size_t i = 0; i < 2; ++i) {
        if (output.steps[0][i].item_b.enchantments.find(Ench(0, 5))
            != output.steps[0][i].item_b.enchantments.end())
            has_sharp5 = true;
        if (output.steps[0][i].item_b.enchantments.find(Ench(1, 2))
            != output.steps[0][i].item_b.enchantments.end())
            has_knock2 = true;
    }
    expect(has_sharp5, "one sacrifice should have sharpness 5");
    expect(has_knock2, "one sacrifice should have knockback 2");

    // After first forge, the base should have the first book's enchantment
    auto has_first_ench = output.steps[0][1].item_a.enchantments.find(Ench(0, 5))
                           != output.steps[0][1].item_a.enchantments.end()
                        || output.steps[0][1].item_a.enchantments.find(Ench(1, 2))
                           != output.steps[0][1].item_a.enchantments.end();
    expect(has_first_ench, "second step base should have the first forged enchantment");

    std::cout << "PASS: test_greedy_forges_items" << std::endl;
}
} // namespace

int main() {
    test_greedy_forges_items();
    std::cout << "All greedy algorithm tests passed!" << std::endl;
    return 0;
}
