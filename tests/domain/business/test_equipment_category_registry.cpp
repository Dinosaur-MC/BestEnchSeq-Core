#include "framework/test_utils.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include <stdexcept>

// All 14 vanilla equipment tags for testing
static std::vector<EquipmentTag> all_builtin_tags() {
    return {
        {EquipmentTag::sword(), "sword"},
        {EquipmentTag::helmet(), "helmet"},
        {EquipmentTag::chestplate(), "chestplate"},
        {EquipmentTag::leggings(), "leggings"},
        {EquipmentTag::boots(), "boots"},
        {EquipmentTag::pickaxe(), "pickaxe"},
        {EquipmentTag::axe(), "axe"},
        {EquipmentTag::shovel(), "shovel"},
        {EquipmentTag::hoe(), "hoe"},
        {EquipmentTag::bow(), "bow"},
        {EquipmentTag::crossbow(), "crossbow"},
        {EquipmentTag::trident(), "trident"},
        {EquipmentTag::shield(), "shield"},
        {EquipmentTag::fishing_rod(), "fishing_rod"},
    };
}

void test_builtins_present() {
    EquipmentTagRegistry categories(all_builtin_tags());

    expect(categories.size() == 14,
           "builtins: should have 14 builtin tags");
    expect(categories.contains(NSID("#minecraft:sword")),
           "builtins: sword should be present");
    expect(!categories.contains(NSID("#minecraft:any")),
           "builtins: 'any' is not a builtin tag");

    std::cout << "PASS: test_builtins_present" << std::endl;
}

void test_lookup_throwing() {
    EquipmentTagRegistry categories(all_builtin_tags());

    bool threw = false;
    try {
        categories.at(NSID("#minecraft:nonexistent"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: at(nonexistent) should throw");

    threw = false;
    try {
        categories.get("nonexistent_tag");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "lookup: get(\"nonexistent\") should throw");

    expect(!categories.contains(NSID("#minecraft:nonexistent")),
           "lookup: contains(\"#minecraft:nonexistent\") should be false");

    std::cout << "PASS: test_lookup_throwing" << std::endl;
}

void test_custom_categories() {
    auto base = all_builtin_tags();
    base.push_back({NSID("#minecraft:mace"), "mace"});
    base.push_back({NSID("#minecraft:wand"), "wand"});
    EquipmentTagRegistry categories(base);

    expect(categories.size() == 16,
           "custom: size should be 16 (14 builtin + 2 custom)");

    expect(categories.contains(NSID("#minecraft:mace")),
           "custom: mace should be present");
    expect(categories.contains(NSID("#minecraft:wand")),
           "custom: wand should be present");

    std::cout << "PASS: test_custom_categories" << std::endl;
}

void test_duplicate_custom_skipped() {
    auto base = all_builtin_tags();
    base.push_back({NSID("#minecraft:custom_item"), "custom_item"});
    // boots already exists in builtins, but insert() will reject duplicates
    EquipmentTagRegistry categories(base);
    categories.insert({EquipmentTag::boots(), "boots"});

    expect(categories.size() == 15,
           "duplicate: size should be 15 (boots insert rejected)");

    // boots should still be present
    expect(categories.contains(NSID("#minecraft:boots")),
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
