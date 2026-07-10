#include "test_utils.h"
#include "registries/EquipmentCategoryRegistry.h"
#include <stdexcept>

void test_builtins_present() {
    EquipmentCategoryRegistry::get_instance().initialize();

    expect(EquipmentCategoryRegistry::get_instance().size() == 15,
           "builtins: should have 15 builtin categories");
    expect(EquipmentCategoryRegistry::get_instance().get_id("sword") ==
               EquipmentCategoryRegistry::ID_SWORD,
           "builtins: sword id should match ID_SWORD");
    expect(EquipmentCategoryRegistry::get_instance().get_id("any") ==
               EquipmentCategoryRegistry::ID_ANY,
           "builtins: any id should match ID_ANY");

    auto& cat = EquipmentCategoryRegistry::get_instance().get("chestplate");
    expect(cat.id == EquipmentCategoryRegistry::ID_CHESTPLATE,
           "builtins: chestplate id should match ID_CHESTPLATE");
    expect(cat.name_id == "chestplate",
           "builtins: chestplate name_id should be chestplate");

    std::cout << "PASS: test_builtins_present" << std::endl;
}

void test_lookup_throwing() {
    EquipmentCategoryRegistry::get_instance().initialize();

    bool threw = false;
    try {
        EquipmentCategoryRegistry::get_instance().get(999);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(999) should throw");

    threw = false;
    try {
        EquipmentCategoryRegistry::get_instance().get("nonexistent_category");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(\"nonexistent\") should throw");

    expect(EquipmentCategoryRegistry::get_instance().get_id("nonexistent") == -1,
           "lookup: get_id(\"nonexistent\") should return -1");

    std::cout << "PASS: test_lookup_throwing" << std::endl;
}

void test_custom_categories() {
    // Re-initialize with custom names
    EquipmentCategoryRegistry::get_instance().initialize(
        {"mace", "wand"}
    );

    // 15 builtins + 2 custom = 17
    expect(EquipmentCategoryRegistry::get_instance().size() == 17,
           "custom: size should be 17 (15 builtin + 2 custom)");

    int32_t mace_id = EquipmentCategoryRegistry::get_instance().get_id("mace");
    expect(mace_id >= 15, "custom: mace should have id >= 15");

    auto& mace = EquipmentCategoryRegistry::get_instance().get("mace");
    expect(mace.name_id == "mace", "custom: mace name_id should be mace");
    expect(mace.id == mace_id, "custom: mace id should match");

    std::cout << "PASS: test_custom_categories" << std::endl;
}

void test_duplicate_custom_skipped() {
    // "boots" is a builtin, should be skipped as custom
    EquipmentCategoryRegistry::get_instance().initialize(
        {"boots", "custom_item"}
    );

    // 15 builtins + 1 custom (boots skipped) = 16
    expect(EquipmentCategoryRegistry::get_instance().size() == 16,
           "duplicate: size should be 16 (boots skipped)");

    // boots should still have the builtin ID
    expect(EquipmentCategoryRegistry::get_instance().get_id("boots") ==
               EquipmentCategoryRegistry::ID_BOOTS,
           "duplicate: boots should still have builtin ID");

    std::cout << "PASS: test_duplicate_custom_skipped" << std::endl;
}

int main() {
    test_builtins_present();
    test_lookup_throwing();
    test_custom_categories();
    test_duplicate_custom_skipped();
    std::cout << "All EquipmentCategoryRegistry tests passed!" << std::endl;
    return 0;
}
