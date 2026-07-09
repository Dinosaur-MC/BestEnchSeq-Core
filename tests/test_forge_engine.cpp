#include "test_utils.h"
#include "algorithm/DefaultForgeEngine.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {
void setup_enchinfo() {
    // Minimal enchantment registry for forge tests
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                     1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                     2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    // Add incompatibility test: sharpness and bane_of_arthropods are mutually exclusive
    infos.push_back({"bane_of_arthropods", "Bane of Arthropods", platform::MCE::All, 5, 5,
                     1, {"sharpness"}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

void test_forge_books() {
    setup_enchinfo();
    DefaultForgeEngine engine(ForgeConfig{});

    // Forge two books: sharpness 4 + sharpness 3 -> sharpness 4 (max, not sum)
    ItemStack book_a(EnchSet{Ench(0, 4)});
    ItemStack book_b(EnchSet{Ench(0, 3)});

    auto [result, cost] = engine.forge(book_a, book_b);
    expect(result.is_book(), "result should be book");
    auto it = result.enchantments.find(Ench(0, 4));
    expect(it != result.enchantments.end(), "result should have sharpness 4");
    expect(cost == 4, "forge cost should be 4 for sharpness 4 + 3 -> 4 (Java, multiplier*4)");
    std::cout << "PASS: test_forge_books (cost=" << cost << ")" << std::endl;
}

void test_forge_equipment_with_book() {
    setup_enchinfo();
    DefaultForgeEngine engine(ForgeConfig{});

    ItemStack sword_item(&sword, EnchSet{});
    ItemStack book(EnchSet{Ench(0, 5)});

    auto [result, cost] = engine.forge(sword_item, book);
    expect(result.is_equipment(), "result should be equipment");
    auto it = result.enchantments.find(Ench(0, 5));
    expect(it != result.enchantments.end(), "result should have sharpness 5");
    expect(cost == 5, "forge cost should be 5 for adding sharpness 5 to empty sword (Java)");
    std::cout << "PASS: test_forge_equipment_with_book (cost=" << cost << ")" << std::endl;
}

void test_forge_incompatible_rejected() {
    setup_enchinfo();
    DefaultForgeEngine engine(ForgeConfig{});

    // Sharpness + Bane of Arthropods are incompatible
    ItemStack sword_item(&sword, EnchSet{Ench(0, 5)});
    ItemStack book(EnchSet{Ench(2, 4)});  // bane_of_arthropods

    auto [result, cost] = engine.forge(sword_item, book);
    // Bane should not be applied
    auto it = result.enchantments.find(Ench(2, 4));
    expect(it == result.enchantments.end(), "incompatible enchant should not be applied");
    expect(cost == 1, "forge cost should be 1 for incompatible enchant (Java penalty)");
    std::cout << "PASS: test_forge_incompatible_rejected (cost=" << cost << ")" << std::endl;
}

void test_forge_not_forgeable_throws() {
    setup_enchinfo();
    DefaultForgeEngine engine(ForgeConfig{});
    ItemStack book(EnchSet{Ench(0, 1)});
    // Create an item that is neither equipment nor book (equipment=nullptr, durability>0)
    ItemStack invalid_item(nullptr, EnchSet{}, 0, 1);
    try {
        engine.forge(invalid_item, book);
        expect(false, "should have thrown for non-forgeable items");
    } catch (const std::invalid_argument&) {
        std::cout << "PASS: test_forge_not_forgeable_throws" << std::endl;
    }
}

void test_ignore_cost_cap() {
    setup_enchinfo();
    ForgeConfig cfg;
    cfg.ignore_cost_cap = true;
    DefaultForgeEngine engine(cfg);
    expect(engine.get_config().ignore_cost_cap, "config should reflect ignore_cost_cap");
    std::cout << "PASS: test_ignore_cost_cap" << std::endl;
}
} // namespace

int main() {
    test_forge_books();
    test_forge_equipment_with_book();
    test_forge_incompatible_rejected();
    test_forge_not_forgeable_throws();
    test_ignore_cost_cap();
    std::cout << "All forge engine tests passed!" << std::endl;
    return 0;
}
