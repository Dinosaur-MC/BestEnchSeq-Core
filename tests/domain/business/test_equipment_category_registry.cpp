#include "framework/test_utils.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include <stdexcept>

void test_builtins_present() {
    EquipmentTagRegistry categories;
    categories.initialize();

    expect(categories.size() == 14,
           "builtins: should have 14 builtin tags");
    expect(categories.get_id("sword") >= 0,
           "builtins: sword should be present");
    expect(categories.get_id("any") == -1,
           "builtins: 'any' is not a builtin tag");

    std::cout << "PASS: test_builtins_present" << std::endl;
}

void test_lookup_throwing() {
    EquipmentTagRegistry categories;
    categories.initialize();

    bool threw = false;
    try {
        categories.at(999);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: at(999) should throw");

    threw = false;
    try {
        categories.get("nonexistent_tag");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(\"nonexistent\") should throw");

    expect(categories.get_id("nonexistent") == -1,
           "lookup: get_id(\"nonexistent\") should return -1");

    std::cout << "PASS: test_lookup_throwing" << std::endl;
}

void test_custom_categories() {
    EquipmentTagRegistry categories;
    categories.initialize({"mace", "wand"});

    expect(categories.size() >= 16,
           "custom: size should be >= 16 (14 builtin + 2 custom)");

    int32_t mace_id = categories.get_id("mace");
    expect(mace_id >= 14, "custom: mace should have id >= 14");

    std::cout << "PASS: test_custom_categories" << std::endl;
}

void test_duplicate_custom_skipped() {
    EquipmentTagRegistry categories;
    categories.initialize({"boots", "custom_item"});

    expect(categories.size() == 15,
           "duplicate: size should be 15 (boots skipped)");

    // boots should still be present at its builtin index
    expect(categories.get_id("boots") >= 0,
           "duplicate: boots should still be present");

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
