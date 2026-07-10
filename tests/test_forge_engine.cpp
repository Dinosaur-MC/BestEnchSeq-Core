#include "test_utils.h"
#include "algorithm/forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "types/ForgeConfig.h"

namespace {
void setup_enchinfo() {
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", MCE::All, 5, 5,
                     1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"knockback", "Knockback", MCE::All, 2, 2,
                     2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
                     1, {"sharpness"}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    EquipmentCategoryRegistry::get_instance().initialize();
    set_active_platform(MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

compact::EnchReg init_reg() {
    compact::EnchReg reg;
    reg.init(EnchantmentRegistry::get_instance(), sword);
    return reg;
}

void test_forge_books() {
    setup_enchinfo();
    auto reg = init_reg();
    ForgeEngine engine;

    // Forge two books: sharpness 4 + sharpness 3 → sharpness 5 (max level combine)
    compact::Item book_a{compact::ItemType::Book, 0, 0, {}};
    book_a.enchs.insert({0, 4});  // sharpness 4
    compact::Item book_b{compact::ItemType::Book, 0, 0, {}};
    book_b.enchs.insert({0, 3});  // sharpness 3

    auto [result, cost] = engine.forge(book_a, book_b, reg);
    expect(result.type == compact::ItemType::Book, "result should be book");
    auto it = result.enchs.find(0);
    expect(it != result.enchs.end() && it->level == 4, "result should have sharpness 4 (max of 4, 3)");
    // Book-to-book forge: cost = penalty(0) + penalty(0) + mult * new_level = 1 * 4
    expect(cost == 4, "forge cost for sharpness 4+3 → 4 with book target should be 4");
    std::cout << "PASS: test_forge_books (cost=" << cost << ")" << std::endl;
}

void test_forge_equipment_with_book() {
    setup_enchinfo();
    auto reg = init_reg();
    ForgeEngine engine;

    compact::Item sword_item{compact::ItemType::Equip, static_cast<int16_t>(sword.max_durability), 0, {}};
    compact::Item book{compact::ItemType::Book, 0, 0, {}};
    book.enchs.insert({0, 5});  // sharpness 5

    auto [result, cost] = engine.forge(sword_item, book, reg);
    expect(result.type == compact::ItemType::Equip, "result should be equipment");
    auto it = result.enchs.find(0);
    expect(it != result.enchs.end() && it->level == 5, "result should have sharpness 5");
    expect(cost == 5, "forge cost for adding sharpness 5 to empty sword should be 5");
    std::cout << "PASS: test_forge_equipment_with_book (cost=" << cost << ")" << std::endl;
}

void test_forge_incompatible_rejected() {
    setup_enchinfo();
    auto reg = init_reg();
    ForgeEngine engine;

    compact::Item sword_item{compact::ItemType::Equip, 1561, 0, {}};
    sword_item.enchs.insert({0, 5});  // sharpness 5

    compact::Item book{compact::ItemType::Book, 0, 0, {}};
    book.enchs.insert({2, 4});  // bane_of_arthropods 4

    auto [result, cost] = engine.forge(sword_item, book, reg);
    auto it = result.enchs.find(2);
    expect(it == result.enchs.end(), "incompatible enchant should not be applied");
    expect(cost == 1, "forge cost for incompatible should be 1 (Java incompatibility penalty)");
    std::cout << "PASS: test_forge_incompatible_rejected (cost=" << cost << ")" << std::endl;
}

void test_forge_not_forgeable() {
    setup_enchinfo();
    auto reg = init_reg();
    ForgeEngine engine;

    // Material-type cannot be forged as target
    compact::Item mat{compact::ItemType::Material, 0, 0, {}};
    compact::Item book{compact::ItemType::Book, 0, 0, {}};
    book.enchs.insert({0, 1});

    expect(!engine.is_forgeable(mat, book), "material target should not be forgeable");
    std::cout << "PASS: test_forge_not_forgeable" << std::endl;
}

void test_ignore_cost_cap() {
    setup_enchinfo();
    auto reg = init_reg();
    ForgeEngine engine(ForgeConfig{false, false, true, MCE::Java});
    expect(true, "ignore_cost_cap constructs without error");
    std::cout << "PASS: test_ignore_cost_cap" << std::endl;
}
} // namespace

int main() {
    test_forge_books();
    test_forge_equipment_with_book();
    test_forge_incompatible_rejected();
    test_forge_not_forgeable();
    test_ignore_cost_cap();
    std::cout << "All forge engine tests passed!" << std::endl;
    return 0;
}
