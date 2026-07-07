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
    std::cout << "PASS: test_greedy_forges_items" << std::endl;
}
} // namespace

int main() {
    test_greedy_forges_items();
    std::cout << "All greedy algorithm tests passed!" << std::endl;
    return 0;
}
