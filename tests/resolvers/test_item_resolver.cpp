#include "framework/test_utils.h"
#include "resolvers/ItemResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"

#include <iostream>
#include <stdexcept>

namespace {

struct TestRegistries {
    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;

    TestRegistries() {
        cat_reg.initialize();

        eq_reg.initialize({Equipment{
            "minecraft:diamond_sword", "Diamond Sword",
            EquipmentCategory::ID_SWORD, 1561
        }});

        std::vector<EnchInfo> infos;
        infos.push_back({"minecraft:sharpness", "Sharpness",
            MCE::All, 5, 5, 1, false, {},
            {EquipmentCategory::ID_SWORD}});
        infos.push_back({"minecraft:knockback", "Knockback",
            MCE::All, 2, 2, 2, false, {},
            {EquipmentCategory::ID_SWORD}});
        infos.push_back({"minecraft:riptide", "Riptide",
            MCE::All, 3, 3, 2, false,
            {"minecraft:sharpness"},
            {EquipmentCategory::ID_TRIDENT}});
        infos.push_back({"minecraft:smite", "Smite",
            MCE::All, 5, 5, 1, false,
            {"minecraft:sharpness"},
            {EquipmentCategory::ID_SWORD}});
        ench_reg.initialize(infos);
    }
};

void test_resolve_basic() {
    TestRegistries regs;
    ItemStack sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    EnchSet target;
    target.emplace(regs.ench_reg.get_id("sharpness"), 5);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.target_item.equipment.has_value(), "equipment preserved");
    expect(result.books.size() == 5,
           "sharpness 5 \xE2\x86\x92 5 graduated books");

    std::cout << "  PASS: test_resolve_basic" << std::endl;
}

void test_resolve_inapplicable_throws() {
    TestRegistries regs;
    ItemStack sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet target;
    target.emplace(regs.ench_reg.get_id("riptide"), 1);

    bool threw = false;
    try {
        ItemResolver::resolve(sword, {}, target, regs.ench_reg);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "inapplicable enchantment should throw");

    std::cout << "  PASS: test_resolve_inapplicable_throws" << std::endl;
}

void test_resolve_conflict_throws() {
    TestRegistries regs;
    ItemStack sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet target;
    target.emplace(regs.ench_reg.get_id("sharpness"), 5);
    target.emplace(regs.ench_reg.get_id("smite"), 5);

    bool threw = false;
    try {
        ItemResolver::resolve(sword, {}, target, regs.ench_reg);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "conflicting enchantments should throw");

    std::cout << "  PASS: test_resolve_conflict_throws" << std::endl;
}

void test_resolve_diff_and_books() {
    TestRegistries regs;
    ItemStack sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    source.emplace(regs.ench_reg.get_id("sharpness"), 3);
    EnchSet target;
    target.emplace(regs.ench_reg.get_id("sharpness"), 5);
    target.emplace(regs.ench_reg.get_id("knockback"), 2);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.books.size() == 4,
           "expected 4 books (sharp 4,5 + knock 1,2)");

    std::cout << "  PASS: test_resolve_diff_and_books" << std::endl;
}

void test_resolve_source_already_has_target() {
    TestRegistries regs;
    ItemStack sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    source.emplace(regs.ench_reg.get_id("sharpness"), 5);
    EnchSet target;
    target.emplace(regs.ench_reg.get_id("sharpness"), 5);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.books.empty(), "no books needed when source already meets target");

    std::cout << "  PASS: test_resolve_source_already_has_target" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== ItemResolver Tests ===" << std::endl;
    try {
        test_resolve_basic();
        test_resolve_inapplicable_throws();
        test_resolve_conflict_throws();
        test_resolve_diff_and_books();
        test_resolve_source_already_has_target();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
