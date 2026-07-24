#include "framework/test_utils.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include "domain/interface/types/RawTypes.h"

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

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, tag_reg, eq_reg, ench_reg);

    expect(tag_reg.contains(NSID("#minecraft:sword")), "sword tag created");
    expect(tag_reg.contains(NSID("#minecraft:axe")), "axe tag created");
    expect(eq_reg.size() == 2, "two equipment registered");
    expect(ench_reg.size() == 1, "one enchantment registered");

    const auto& instances = ench_reg.data();
    expect(instances[0].applicable_equipments.size() == 2,
           "sharpness applicable to 2 categories");

    TEST_PASS("test_resolve_basic");
}

// ============================================================================
// test_resolve_empty
// ============================================================================
void test_resolve_empty() {
    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, {}, tag_reg, eq_reg, ench_reg);

    expect(ench_reg.size() == 0, "no enchantments");
    expect(eq_reg.size() == 0, "no equipment");

    // No equipment provided, so no categories registered.
    expect(tag_reg.size() == 0,
           "tag registry is empty with no equipment");

    // No "sword" tag without equipment specifying that category.
    expect(!tag_reg.contains(NSID("#minecraft:sword")), "no 'sword' tag with no equipment");

    TEST_PASS("test_resolve_empty");
}

// ============================================================================
// test_resolve_category_dedup
// ============================================================================
void test_resolve_category_dedup() {
    std::vector<RawEquipment> equipments;
    equipments.push_back({{"m", "a"}, "A", "sword", 100});
    equipments.push_back({{"m", "b"}, "B", "sword", 200});  // same category

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, equipments, tag_reg, eq_reg, ench_reg);

    expect(tag_reg.contains(NSID("#minecraft:sword")), "sword tag exists");

    // Both equipment should reference the same category NSID
    const auto& instances = eq_reg.data();
    expect(instances.size() == 2, "two equipment registered");
    expect(instances[0].category == instances[1].category,
           "both equipment share same category");

    // Only one unique category ("sword") from equipment definitions
    expect(tag_reg.size() == 1,
           "tag registry has exactly 1 category (sword, no dupes)");

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

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, tag_reg, eq_reg, ench_reg);

    expect(ench_reg.size() == 2, "two enchantments registered");
    const auto& ench_instances = ench_reg.data();
    expect(ench_reg.is_incompatible(ench_instances[0].id, ench_instances[1].id),
           "sharpness and bane are incompatible");
    expect(ench_reg.is_incompatible(ench_instances[1].id, ench_instances[0].id),
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

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve(enchants, equipments, tag_reg, eq_reg, ench_reg);

    const auto& instances = ench_reg.data();
    // "sword" resolves, "nonexistent_category" is silently dropped
    expect(instances[0].applicable_equipments.size() == 1,
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
