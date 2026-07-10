#include "test_utils.h"
#include "registries/CompactedRegistries.h"
#include "registries/RegistryAccess.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/ForgeConfig.h"
#include <stdexcept>

namespace {

void setup() {
    registries::categories().initialize();
    registries::enchants().initialize({
        {"sharpness", "Sharpness", MCE::All, 5, 5,
         1, {}, {EquipmentCategoryRegistry::ID_SWORD}},
        {"knockback", "Knockback", MCE::All, 2, 2,
         2, {}, {EquipmentCategoryRegistry::ID_SWORD}},
        {"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
         1, {"sharpness"}, {EquipmentCategoryRegistry::ID_SWORD}},
        {"protection", "Protection", MCE::All, 4, 4,
         1, {}, {EquipmentCategoryRegistry::ID_CHESTPLATE}},
    });
}

void test_basic_init_and_size() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategoryRegistry::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // 4 enchantments total, but only 3 applicable to sword
    // Actually init() takes ALL from the registry, applicability is per-ench
    expect(reg.size() == 4, "size: should have 4 enchantments");
    expect(reg.get_target_equip().name_id == "diamond_sword",
           "target: should be diamond_sword");

    std::cout << "PASS: test_basic_init_and_size" << std::endl;
}

void test_safe_get_bounds() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategoryRegistry::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // Valid access via .at() path
    expect(reg.get(0).mul > 0, "get(0): multiplier should be > 0");

    // Out-of-range access via .at() should throw
    bool threw = false;
    try {
        reg.get(static_cast<int16_t>(reg.size()));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on out-of-range id");

    // Negative id via .at() should throw
    threw = false;
    try {
        reg.get(static_cast<int16_t>(-1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on negative id");

    std::cout << "PASS: test_safe_get_bounds" << std::endl;
}

void test_conflict_detection() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategoryRegistry::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // sharpness(0) and bane_of_arthropods(2) should conflict
    expect(reg.is_conflict(0, 2), "conflict: sharpness vs bane should conflict");
    expect(reg.is_conflict(2, 0), "conflict: bane vs sharpness should conflict (symmetric)");

    // sharpness(0) and knockback(1) should NOT conflict
    expect(!reg.is_conflict(0, 1), "conflict: sharpness vs knockback should NOT conflict");

    // Self-check should NOT conflict
    expect(!reg.is_conflict(0, 0), "conflict: self should NOT conflict");
    expect(!reg.is_conflict(1, 1), "conflict: self should NOT conflict");

    std::cout << "PASS: test_conflict_detection" << std::endl;
}

void test_multiplier_and_max_level() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategoryRegistry::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // sharpness: mult=1, max_lvl=5
    expect(reg.get_multiplier(0) == 1, "multiplier: sharpness should be 1");
    expect(reg.get_max_level(0) == 5, "max_level: sharpness should be 5");

    // knockback: mult=2, max_lvl=2
    expect(reg.get_multiplier(1) == 2, "multiplier: knockback should be 2");
    expect(reg.get_max_level(1) == 2, "max_level: knockback should be 2");

    std::cout << "PASS: test_multiplier_and_max_level" << std::endl;
}

} // anonymous namespace

int main() {
    test_basic_init_and_size();
    test_safe_get_bounds();
    test_conflict_detection();
    test_multiplier_and_max_level();
    std::cout << "All EnchReg tests passed!" << std::endl;
    return 0;
}
