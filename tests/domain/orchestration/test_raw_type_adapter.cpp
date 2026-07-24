#include "framework/test_utils.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/interface/types/RawTypes.h"

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

    // Verify the single enchantment via NSID lookup
    const auto& ench = ench_reg.at(NSID("minecraft:sharpness"));
    expect(ench.applicable_equipments.size() == 2,
           "sharpness applicable to 2 categories");

    TEST_PASS("test_resolve_basic");
}

// ============================================================================
// test_resolve_empty
// ============================================================================
void test_resolve_empty() {
    std::vector<RawEquipment> equipments;
    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, equipments, tag_reg, eq_reg, ench_reg);

    expect(tag_reg.empty(), "no tags");
    expect(eq_reg.empty(), "no equipment");
    expect(ench_reg.empty(), "no enchantments");

    TEST_PASS("test_resolve_empty");
}

// ============================================================================
// test_resolve_category_dedup
// ============================================================================
void test_resolve_category_dedup() {
    std::vector<RawEquipment> equipments;
    equipments.push_back({{"minecraft", "wooden_sword"}, "Wooden Sword", "sword", 60});
    equipments.push_back({{"minecraft", "stone_sword"}, "Stone Sword", "sword", 132});

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RawTypeAdapter::resolve({}, equipments, tag_reg, eq_reg, ench_reg);

    expect(tag_reg.contains(NSID("#minecraft:sword")), "sword tag exists");

    // Both equipment should reference the same category NSID
    const auto& eq_map = eq_reg.data();
    expect(eq_map.size() == 2, "two equipment registered");

    // Verify both equipment reference the same category
    NSID shared_category;
    bool first = true;
    bool all_same = true;
    for (const auto& [id, eq] : eq_map) {
        if (first) {
            shared_category = eq.category;
            first = false;
        } else if (eq.category != shared_category) {
            all_same = false;
        }
    }
    expect(all_same, "both equipment share same category");

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
    auto sharp_it = ench_reg.find(NSID("minecraft:sharpness"));
    auto bane_it  = ench_reg.find(NSID("minecraft:bane_of_arthropods"));
    expect(sharp_it != ench_reg.end(), "sharpness found");
    expect(bane_it != ench_reg.end(), "bane found");

    expect(ench_reg.is_incompatible(sharp_it->id, bane_it->id),
           "sharpness and bane are incompatible");
    expect(ench_reg.is_incompatible(bane_it->id, sharp_it->id),
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

    // "sword" resolves, "nonexistent_category" is silently dropped
    const auto& ench = ench_reg.at(NSID("minecraft:sharpness"));
    expect(ench.applicable_equipments.size() == 1,
           "only 'sword' category resolved");

    TEST_PASS("test_resolve_applicable_items_unknown_category");
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "=== RawTypeAdapter Tests ===" << std::endl;
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
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
