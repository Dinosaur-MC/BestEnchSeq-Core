#include "framework/test_utils.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include <stdexcept>

void test_builtins_present() {
    EquipmentCategoryRegistry categories;
    categories.initialize();

    expect(categories.size() == 15,
           "builtins: should have 15 builtin categories");
    expect(categories.get_id("sword") ==
               EquipmentCategory::ID_SWORD,
           "builtins: sword id should match ID_SWORD");
    expect(categories.get_id("any") ==
               EquipmentCategory::ID_ANY,
           "builtins: any id should match ID_ANY");

    auto& cat = categories.get("chestplate");
    expect(cat.id == EquipmentCategory::ID_CHESTPLATE,
           "builtins: chestplate id should match ID_CHESTPLATE");
    expect(cat.name_id == "chestplate",
           "builtins: chestplate name_id should be chestplate");

    std::cout << "PASS: test_builtins_present" << std::endl;
}

void test_lookup_throwing() {
    EquipmentCategoryRegistry categories;
    categories.initialize();

    bool threw = false;
    try {
        categories.get(999);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(999) should throw");

    threw = false;
    try {
        categories.get("nonexistent_category");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(\"nonexistent\") should throw");

    expect(categories.get_id("nonexistent") == -1,
           "lookup: get_id(\"nonexistent\") should return -1");

    std::cout << "PASS: test_lookup_throwing" << std::endl;
}

void test_custom_categories() {
    EquipmentCategoryRegistry categories;
    categories.initialize({"mace", "wand"});

    // 15 builtins + 2 custom = 17
    expect(categories.size() == 17,
           "custom: size should be 17 (15 builtin + 2 custom)");

    int32_t mace_id = categories.get_id("mace");
    expect(mace_id >= 15, "custom: mace should have id >= 15");

    auto& mace = categories.get("mace");
    expect(mace.name_id == "mace", "custom: mace name_id should be mace");
    expect(mace.id == mace_id, "custom: mace id should match");

    std::cout << "PASS: test_custom_categories" << std::endl;
}

void test_duplicate_custom_skipped() {
    EquipmentCategoryRegistry categories;
    categories.initialize({"boots", "custom_item"});

    // 15 builtins + 1 custom (boots skipped) = 16
    expect(categories.size() == 16,
           "duplicate: size should be 16 (boots skipped)");

    // boots should still have the builtin ID
    expect(categories.get_id("boots") ==
               EquipmentCategory::ID_BOOTS,
           "duplicate: boots should still have builtin ID");

    std::cout << "PASS: test_duplicate_custom_skipped" << std::endl;
}

int main() {
    try {
        test_builtins_present();
        test_lookup_throwing();
        test_custom_categories();
        test_duplicate_custom_skipped();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
