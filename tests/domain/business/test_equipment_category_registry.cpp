#define BESQ_TEST_MAIN
#include "domain/business/registries/TagRegistry.h"
#include "framework/test_framework.h"
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

TEST_CASE("test_builtins_present") {
    TagRegistry categories(all_builtin_tags());

    expect(categories.size() == 14, "builtins: should have 14 builtin tags");
    expect(categories.contains(NSID("#minecraft:sword")), "builtins: sword should be present");
    expect(!categories.contains(NSID("#minecraft:any")), "builtins: 'any' is not a builtin tag");

    std::cout << "PASS: test_builtins_present" << std::endl;
}

TEST_CASE("test_lookup_throwing") {
    TagRegistry categories(all_builtin_tags());

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

TEST_CASE("test_custom_categories") {
    auto base = all_builtin_tags();
    base.push_back({NSID("#minecraft:mace"), "mace"});
    base.push_back({NSID("#minecraft:wand"), "wand"});
    TagRegistry categories(base);

    expect(categories.size() == 16, "custom: size should be 16 (14 builtin + 2 custom)");

    expect(categories.contains(NSID("#minecraft:mace")), "custom: mace should be present");
    expect(categories.contains(NSID("#minecraft:wand")), "custom: wand should be present");

    std::cout << "PASS: test_custom_categories" << std::endl;
}

TEST_CASE("test_duplicate_custom_skipped") {
    auto base = all_builtin_tags();
    base.push_back({NSID("#minecraft:custom_item"), "custom_item"});
    // boots already exists in builtins, but insert() will reject duplicates
    TagRegistry categories(base);
    categories.insert({EquipmentTag::boots(), "boots"});

    expect(categories.size() == 15, "duplicate: size should be 15 (boots insert rejected)");

    // boots should still be present
    expect(categories.contains(NSID("#minecraft:boots")), "duplicate: boots should still be present");

    std::cout << "PASS: test_duplicate_custom_skipped" << std::endl;
}
