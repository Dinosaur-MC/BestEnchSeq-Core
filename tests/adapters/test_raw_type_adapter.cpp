#include "framework/test_utils.h"
#include "adapters/RawTypeAdapter.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include "types/RawTypes.h"

#include <cstdint>
#include <iostream>
#include <vector>

// ============================================================================
// test_resolve_basic
// ============================================================================
void test_resolve_basic() {
    std::vector<RawEnchantment> enchants;
    RawEnchantment sharp;
    sharp.id = Id{"minecraft", "sharpness"};
    sharp.display_name = "Sharpness";
    sharp.max_level = 5;
    sharp.multiplier = 1;
    sharp.applicable_items = {"sword", "axe"};
    enchants.push_back(std::move(sharp));

    std::vector<RawEquipment> equipments;
    equipments.push_back({{"minecraft", "diamond_sword"}, "Diamond Sword", "sword", 1561});
    equipments.push_back({{"minecraft", "diamond_axe"}, "Diamond Axe", "axe", 1561});

    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, cat_reg, eq_reg, ench_reg);

    expect(cat_reg.get_id("sword") >= 0, "sword category created");
    expect(cat_reg.get_id("axe") >= 0, "axe category created");
    expect(eq_reg.size() == 2, "two equipment registered");
    expect(ench_reg.size() == 1, "one enchantment registered");

    const auto& instances = ench_reg.get_instances();
    expect(instances[0].applicable_category_ids.size() == 2,
           "sharpness applicable to 2 categories");

    TEST_PASS("test_resolve_basic");
}

// ============================================================================
// test_resolve_empty
// ============================================================================
void test_resolve_empty() {
    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, {}, cat_reg, eq_reg, ench_reg);

    expect(ench_reg.size() == 0, "no enchantments");
    expect(eq_reg.size() == 0, "no equipment");

    // Category registry always has builtin categories (15: "any" + 14 specific).
    expect(cat_reg.size() == 15,
           "category registry has 15 builtin categories");

    // Builtin "any" category is always present.
    expect(cat_reg.get_id("any") >= 0, "builtin 'any' category exists");

    TEST_PASS("test_resolve_empty");
}

// ============================================================================
// test_resolve_category_dedup
// ============================================================================
void test_resolve_category_dedup() {
    std::vector<RawEquipment> equipments;
    equipments.push_back({{"m", "a"}, "A", "sword", 100});
    equipments.push_back({{"m", "b"}, "B", "sword", 200});  // same category

    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, equipments, cat_reg, eq_reg, ench_reg);

    int32_t sword_id = cat_reg.get_id("sword");
    expect(sword_id >= 0, "sword category exists");

    // Both equipment should reference the same category id
    const auto& instances = eq_reg.get_instances();
    expect(instances.size() == 2, "two equipment registered");
    expect(instances[0].category_id == instances[1].category_id,
           "both equipment share same category_id");

    // category "sword" is a builtin, so no duplicate was added
    // Only register "sword" once — means 15 builtin categories
    expect(cat_reg.size() == 15,
           "category registry has exactly the 15 builtin categories (no dupes)");

    TEST_PASS("test_resolve_category_dedup");
}

// ============================================================================
// test_resolve_enchantment_exclusive_set
// ============================================================================
void test_resolve_enchantment_exclusive_set() {
    std::vector<RawEnchantment> enchants;

    // sharpness is exclusive with bane_of_arthropods
    RawEnchantment sharp;
    sharp.id = Id{"minecraft", "sharpness"};
    sharp.display_name = "Sharpness";
    sharp.max_level = 5;
    sharp.multiplier = 1;
    sharp.exclusive_set = {"minecraft:bane_of_arthropods"};
    enchants.push_back(std::move(sharp));

    RawEnchantment bane;
    bane.id = Id{"minecraft", "bane_of_arthropods"};
    bane.display_name = "Bane of Arthropods";
    bane.max_level = 5;
    bane.multiplier = 1;
    bane.exclusive_set = {"minecraft:sharpness"};
    enchants.push_back(std::move(bane));

    std::vector<RawEquipment> equipments;
    equipments.push_back({{"minecraft", "diamond_sword"}, "Diamond Sword", "sword", 1561});

    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, cat_reg, eq_reg, ench_reg);

    expect(ench_reg.size() == 2, "two enchantments registered");
    expect(ench_reg.is_incompatible(0, 1),
           "sharpness and bane are incompatible");
    expect(ench_reg.is_incompatible(1, 0),
           "bane and sharpness are incompatible (symmetry)");

    TEST_PASS("test_resolve_enchantment_exclusive_set");
}

// ============================================================================
// test_resolve_applicable_items_unknown_category
// ============================================================================
void test_resolve_applicable_items_unknown_category() {
    std::vector<RawEnchantment> enchants;
    RawEnchantment sharp;
    sharp.id = Id{"minecraft", "sharpness"};
    sharp.display_name = "Sharpness";
    sharp.max_level = 5;
    sharp.multiplier = 1;
    sharp.applicable_items = {"sword", "nonexistent_category"};
    enchants.push_back(std::move(sharp));

    std::vector<RawEquipment> equipments;
    equipments.push_back({{"minecraft", "diamond_sword"}, "Diamond Sword", "sword", 1561});

    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, cat_reg, eq_reg, ench_reg);

    const auto& instances = ench_reg.get_instances();
    // "sword" resolves, "nonexistent_category" is silently dropped
    expect(instances[0].applicable_category_ids.size() == 1,
           "only 'sword' category resolved");

    TEST_PASS("test_resolve_applicable_items_unknown_category");
}

// ============================================================================
// main
// ============================================================================
int main() {
    try {
        test_resolve_basic();
        test_resolve_empty();
        test_resolve_category_dedup();
        test_resolve_enchantment_exclusive_set();
        test_resolve_applicable_items_unknown_category();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
