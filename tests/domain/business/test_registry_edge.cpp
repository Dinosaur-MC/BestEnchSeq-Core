#include "framework/test_utils.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"

namespace {

// ══════════════════════════════════════════════════════════════════════════
// Section A — Incompatibility table consistency
// ══════════════════════════════════════════════════════════════════════════

void test_insert_with_exclusive() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:smite")},
                   std::unordered_set<NSID>{}};
    EnchInfo smite{NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};

    reg.insert(sharp);
    reg.insert(smite);

    expect(reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "sharpness and smite should be incompatible");
    expect(reg.is_incompatible(NSID("minecraft:smite"), NSID("minecraft:sharpness")),
           "smite and sharpness should be incompatible (symmetric)");
    TEST_PASS("test_insert_with_exclusive");
}

void test_insert_or_assign_new() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};

    auto [it, inserted] = reg.insert_or_assign(sharp);
    (void)it;
    expect(inserted, "insert_or_assign of new enchantment should return true");
    expect(reg.size() == 1, "size should be 1 after insert_or_assign");
    expect(reg.contains(NSID("minecraft:sharpness")), "sharpness should be findable");
    TEST_PASS("test_insert_or_assign_new");
}

void test_insert_or_assign_update_exclusive() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:smite")},
                   std::unordered_set<NSID>{}};
    EnchInfo smite{NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};

    reg.insert(sharp);
    reg.insert(smite);
    expect(reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "initially sharpness and smite should be incompatible");

    // insert_or_assign sharpness with a new exclusive_set {bane}
    EnchInfo sharp_updated{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                           std::unordered_set<NSID>{NSID("minecraft:bane_of_arthropods")},
                           std::unordered_set<NSID>{}};
    EnchInfo bane{NSID("minecraft:bane_of_arthropods"), "Bane of Arthropods", MCE::All, 5, 5, 1, false,
                  std::unordered_set<NSID>{},
                  std::unordered_set<NSID>{}};
    reg.insert(bane);

    auto [it, assigned] = reg.insert_or_assign(sharp_updated);
    (void)it;
    expect(!assigned, "insert_or_assign of existing should return false (assigned)");

    // New incompatibility should be present
    expect(reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:bane_of_arthropods")),
           "sharpness and bane should now be incompatible");
    // Old incompatibility should be removed
    expect(!reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "sharpness and smite should no longer be incompatible after reassign");
    TEST_PASS("test_insert_or_assign_update_exclusive");
}

void test_update_exclusive_set() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:smite")},
                   std::unordered_set<NSID>{}};
    EnchInfo smite{NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:sharpness")},
                   std::unordered_set<NSID>{}};

    reg.insert(sharp);
    reg.insert(smite);
    expect(reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "initially incompatible");

    // Update sharpness with empty exclusive_set
    EnchInfo sharp_patched = sharp;
    sharp_patched.exclusive_set.clear();
    bool ok = reg.update(sharp_patched);
    expect(ok, "update should succeed");

    expect(!reg.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "after update, sharpness and smite should not be incompatible");
    expect(!reg.is_incompatible(NSID("minecraft:smite"), NSID("minecraft:sharpness")),
           "after update, smite and sharpness should not be incompatible either");
    TEST_PASS("test_update_exclusive_set");
}

void test_erase_cascades() {
    EnchantmentRegistry reg;

    EnchInfo enchA{NSID("minecraft:ench_a"), "Ench A", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:ench_b"), NSID("minecraft:ench_c")},
                   std::unordered_set<NSID>{}};
    EnchInfo enchB{NSID("minecraft:ench_b"), "Ench B", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};
    EnchInfo enchC{NSID("minecraft:ench_c"), "Ench C", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};

    reg.insert(enchA);
    reg.insert(enchB);
    reg.insert(enchC);

    expect(reg.is_incompatible(NSID("minecraft:ench_a"), NSID("minecraft:ench_b")),
           "A should be incompatible with B");
    expect(reg.is_incompatible(NSID("minecraft:ench_b"), NSID("minecraft:ench_a")),
           "B should be incompatible with A (symmetric)");

    bool ok = reg.erase(NSID("minecraft:ench_a"));
    expect(ok, "erase of A should succeed");

    expect(!reg.is_incompatible(NSID("minecraft:ench_a"), NSID("minecraft:ench_b")),
           "after erase, is_incompatible(A,B) should be false (A gone)");
    expect(!reg.is_incompatible(NSID("minecraft:ench_b"), NSID("minecraft:ench_a")),
           "after erase, is_incompatible(B,A) should be false (B's entry cleaned)");
    TEST_PASS("test_erase_cascades");
}

void test_clear_resets_incompatible() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{NSID("minecraft:smite")},
                   std::unordered_set<NSID>{}};
    EnchInfo smite{NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{},
                   std::unordered_set<NSID>{}};

    reg.insert(sharp);
    reg.insert(smite);
    expect(reg.size() == 2, "size should be 2 before clear");

    reg.clear();
    expect(reg.size() == 0, "size should be 0 after clear");
    expect(reg.get_exclusive_set(NSID("minecraft:sharpness")).empty(),
           "exclusive set for any NSID should be empty after clear");
    TEST_PASS("test_clear_resets_incompatible");
}

// ══════════════════════════════════════════════════════════════════════════
// Section B — insert_or_assign on all three registries
// ══════════════════════════════════════════════════════════════════════════

void test_insert_or_assign_equipment() {
    EquipmentRegistry reg;

    Equipment eq{NSID("minecraft:test"), "Original", NSID(), 100};
    auto [it, inserted] = reg.insert_or_assign(eq);
    (void)it;
    expect(inserted, "insert_or_assign new equipment should be inserted");
    expect(reg.size() == 1, "size should be 1");

    // Overwrite with new name and durability
    Equipment eq2{NSID("minecraft:test"), "Overwritten", NSID(), 200};
    auto [it2, assigned] = reg.insert_or_assign(eq2);
    (void)it2;
    expect(!assigned, "insert_or_assign existing should assign (not insert)");
    expect(reg.size() == 1, "size should still be 1");
    expect(reg.at(NSID("minecraft:test")).name == "Overwritten",
           "name should be updated to 'Overwritten'");
    TEST_PASS("test_insert_or_assign_equipment");
}

void test_insert_or_assign_tag() {
    EquipmentTagRegistry reg;

    EquipmentTag tag{EquipmentTag::sword(), "sword"};
    auto [it, inserted] = reg.insert_or_assign(tag);
    (void)it;
    expect(inserted, "insert_or_assign new tag should be inserted");
    expect(reg.size() == 1, "size should be 1");

    // Overwrite name
    EquipmentTag tag2{EquipmentTag::sword(), "updated_sword"};
    auto [it2, assigned] = reg.insert_or_assign(tag2);
    (void)it2;
    expect(!assigned, "insert_or_assign existing tag should assign (not insert)");
    expect(reg.at(EquipmentTag::sword()).name == "updated_sword",
           "tag name should be updated to 'updated_sword'");
    TEST_PASS("test_insert_or_assign_tag");
}

// ══════════════════════════════════════════════════════════════════════════
// Section C — clear / empty / iterator edge cases
// ══════════════════════════════════════════════════════════════════════════

void test_empty_registry() {
    EnchantmentRegistry reg;
    expect(reg.empty(), "default constructed EnchantmentRegistry should be empty");
    expect(reg.size() == 0, "size should be 0");
    expect(reg.begin() == reg.end(), "begin() should equal end() for empty registry");
    TEST_PASS("test_empty_registry");
}

void test_clear_and_refill() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    EnchInfo smite{NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};

    reg.insert(sharp);
    reg.insert(smite);
    expect(reg.size() == 2, "size should be 2 after inserts");

    reg.clear();
    expect(reg.empty(), "should be empty after clear");
    expect(!reg.contains(NSID("minecraft:sharpness")), "sharpness should not be found after clear");

    // Re-insert
    reg.insert(sharp);
    expect(reg.size() == 1, "size should be 1 after re-insert");
    expect(reg.contains(NSID("minecraft:sharpness")), "sharpness should be found after re-insert");
    TEST_PASS("test_clear_and_refill");
}

void test_iterator_walk() {
    EnchantmentRegistry reg;
    for (int i = 1; i <= 5; ++i) {
        std::string nsid = "minecraft:ench_" + std::to_string(i);
        reg.insert(EnchInfo{NSID(nsid), "Ench " + std::to_string(i), MCE::All, 5, 5, 1, false,
                            std::unordered_set<NSID>{}, std::unordered_set<NSID>{}});
    }

    int count = 0;
    for (const auto& ench : reg) {
        (void)ench;
        ++count;
    }
    expect(count == 5, "range-for should count exactly 5 elements");
    TEST_PASS("test_iterator_walk");
}

void test_reinsert_after_erase() {
    EnchantmentRegistry reg;

    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                   std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};

    reg.insert(sharp);
    expect(reg.size() == 1, "size should be 1 after insert");

    reg.erase(NSID("minecraft:sharpness"));
    expect(reg.size() == 0, "size should be 0 after erase");

    // Re-insert the same NSID
    reg.insert(sharp);
    expect(reg.size() == 1, "size should be 1 after re-insert");
    expect(reg.contains(NSID("minecraft:sharpness")), "sharpness should be findable after re-insert");
    TEST_PASS("test_reinsert_after_erase");
}

// ══════════════════════════════════════════════════════════════════════════
// Section D — EquipmentRegistry gaps
// ══════════════════════════════════════════════════════════════════════════

void test_eq_update() {
    EquipmentRegistry reg;

    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    expect(reg.size() == 1, "size should be 1 after insert");

    Equipment updated{NSID("minecraft:diamond_sword"), "Diamond Sword X", EquipmentTag::sword(), 1561};
    bool ok = reg.update(updated);
    expect(ok, "update should succeed");
    expect(reg.at(NSID("minecraft:diamond_sword")).name == "Diamond Sword X",
           "at() should return updated name");
    expect(reg.size() == 1, "size should remain unchanged after update");
    TEST_PASS("test_eq_update");
}

void test_eq_create_subset() {
    EquipmentRegistry reg;

    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    reg.insert(Equipment{NSID("minecraft:iron_sword"), "Iron Sword", EquipmentTag::sword(), 250});
    reg.insert(Equipment{NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe", EquipmentTag::pickaxe(), 1561});

    auto subset = reg.create_subset([](const Equipment& eq) {
        return eq.category == EquipmentTag::sword();
    });

    expect(subset.size() == 2, "sword subset should have 2 items");
    expect(subset.contains(NSID("minecraft:diamond_sword")), "subset should contain diamond_sword");
    expect(subset.contains(NSID("minecraft:iron_sword")), "subset should contain iron_sword");
    TEST_PASS("test_eq_create_subset");
}

void test_eq_clear() {
    EquipmentRegistry reg;

    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    reg.insert(Equipment{NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe", EquipmentTag::pickaxe(), 1561});
    expect(reg.size() == 2, "size should be 2 before clear");

    reg.clear();
    expect(reg.empty(), "should be empty after clear");
    expect(reg.size() == 0, "size should be 0 after clear");
    TEST_PASS("test_eq_clear");
}

void test_eq_data_access() {
    EquipmentRegistry reg;

    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    reg.insert(Equipment{NSID("minecraft:iron_sword"), "Iron Sword", EquipmentTag::sword(), 250});

    const auto& map = reg.data();
    expect(map.size() == 2, "data map size should be 2");
    expect(map.at(NSID("minecraft:diamond_sword")).name == "Diamond Sword",
           "data map key access for diamond_sword");
    expect(map.at(NSID("minecraft:iron_sword")).name == "Iron Sword",
           "data map key access for iron_sword");
    TEST_PASS("test_eq_data_access");
}

void test_eq_iterator() {
    EquipmentRegistry reg;

    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    reg.insert(Equipment{NSID("minecraft:iron_sword"), "Iron Sword", EquipmentTag::sword(), 250});
    reg.insert(Equipment{NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe", EquipmentTag::pickaxe(), 1561});

    int count = 0;
    bool found_diamond_sword = false;
    bool found_iron_sword = false;
    bool found_pickaxe = false;
    for (const auto& eq : reg) {
        if (eq.id == NSID("minecraft:diamond_sword")) found_diamond_sword = true;
        if (eq.id == NSID("minecraft:iron_sword")) found_iron_sword = true;
        if (eq.id == NSID("minecraft:diamond_pickaxe")) found_pickaxe = true;
        ++count;
    }
    expect(count == 3, "range-for should visit exactly 3 items");
    expect(found_diamond_sword, "iteration should include diamond_sword");
    expect(found_iron_sword, "iteration should include iron_sword");
    expect(found_pickaxe, "iteration should include diamond_pickaxe");
    TEST_PASS("test_eq_iterator");
}

// ══════════════════════════════════════════════════════════════════════════
// Section E — EquipmentTagRegistry gaps
// ══════════════════════════════════════════════════════════════════════════

void test_tag_erase() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{EquipmentTag::sword(), "sword"});
    reg.insert(EquipmentTag{EquipmentTag::pickaxe(), "pickaxe"});
    expect(reg.size() == 2, "size should be 2 after inserts");

    bool ok = reg.erase(EquipmentTag::sword());
    expect(ok, "erase should return true");
    expect(reg.size() == 1, "size should be 1 after erase");
    expect(!reg.contains(EquipmentTag::sword()), "erased tag should not be found");
    expect(reg.contains(EquipmentTag::pickaxe()), "remaining tag should still be found");
    TEST_PASS("test_tag_erase");
}

void test_tag_update() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{EquipmentTag::sword(), "sword"});

    EquipmentTag updated{EquipmentTag::sword(), "weapon_sword"};
    bool ok = reg.update(updated);
    expect(ok, "update should succeed");
    expect(reg.at(EquipmentTag::sword()).name == "weapon_sword",
           "name should be updated via at()");
    TEST_PASS("test_tag_update");
}

void test_tag_create_subset() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{NSID("#minecraft:sword"), "sword"});
    reg.insert(EquipmentTag{NSID("#minecraft:diamond_sword"), "diamond_sword"});
    reg.insert(EquipmentTag{NSID("#minecraft:pickaxe"), "pickaxe"});
    reg.insert(EquipmentTag{NSID("#minecraft:axe"), "axe"});

    auto subset = reg.create_subset([](const EquipmentTag& tag) {
        return tag.name.find("sword") != std::string::npos;
    });

    expect(subset.size() == 2, "subset should have 2 tags matching 'sword'");
    expect(subset.contains(NSID("#minecraft:sword")), "subset should contain sword");
    expect(subset.contains(NSID("#minecraft:diamond_sword")), "subset should contain diamond_sword");
    TEST_PASS("test_tag_create_subset");
}

void test_tag_clear() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{EquipmentTag::sword(), "sword"});
    reg.insert(EquipmentTag{EquipmentTag::pickaxe(), "pickaxe"});
    reg.insert(EquipmentTag{EquipmentTag::bow(), "bow"});
    expect(reg.size() == 3, "size should be 3 before clear");

    reg.clear();
    expect(reg.empty(), "should be empty after clear");
    expect(reg.size() == 0, "size should be 0 after clear");
    TEST_PASS("test_tag_clear");
}

void test_tag_data() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{EquipmentTag::sword(), "sword"});
    reg.insert(EquipmentTag{EquipmentTag::pickaxe(), "pickaxe"});

    const auto& map = reg.data();
    expect(map.size() == 2, "data map size should be 2");
    expect(map.at(EquipmentTag::sword()).name == "sword",
           "data map key access for sword");
    expect(map.at(EquipmentTag::pickaxe()).name == "pickaxe",
           "data map key access for pickaxe");
    TEST_PASS("test_tag_data");
}

void test_tag_iterator() {
    EquipmentTagRegistry reg;

    reg.insert(EquipmentTag{EquipmentTag::dummy(), "dummy"});
    reg.insert(EquipmentTag{EquipmentTag::sword(), "sword"});
    reg.insert(EquipmentTag{EquipmentTag::helmet(), "helmet"});
    reg.insert(EquipmentTag{EquipmentTag::chestplate(), "chestplate"});
    reg.insert(EquipmentTag{EquipmentTag::leggings(), "leggings"});
    reg.insert(EquipmentTag{EquipmentTag::boots(), "boots"});
    reg.insert(EquipmentTag{EquipmentTag::pickaxe(), "pickaxe"});
    reg.insert(EquipmentTag{EquipmentTag::axe(), "axe"});
    reg.insert(EquipmentTag{EquipmentTag::shovel(), "shovel"});
    reg.insert(EquipmentTag{EquipmentTag::hoe(), "hoe"});
    reg.insert(EquipmentTag{EquipmentTag::bow(), "bow"});
    reg.insert(EquipmentTag{EquipmentTag::crossbow(), "crossbow"});
    reg.insert(EquipmentTag{EquipmentTag::trident(), "trident"});
    reg.insert(EquipmentTag{EquipmentTag::shield(), "shield"});
    reg.insert(EquipmentTag{EquipmentTag::fishing_rod(), "fishing_rod"});

    int count = 0;
    for (const auto& tag : reg) {
        (void)tag;
        ++count;
    }
    expect(count == 14, "range-for should iterate exactly 14 builtin tags");
    TEST_PASS("test_tag_iterator");
}

} // anonymous namespace

int main() {
    try {
        // Section A — Incompatibility table consistency
        test_insert_with_exclusive();
        test_insert_or_assign_new();
        test_insert_or_assign_update_exclusive();
        test_update_exclusive_set();
        test_erase_cascades();
        test_clear_resets_incompatible();

        // Section B — insert_or_assign on all three registries
        test_insert_or_assign_equipment();
        test_insert_or_assign_tag();

        // Section C — clear / empty / iterator edge cases
        test_empty_registry();
        test_clear_and_refill();
        test_iterator_walk();
        test_reinsert_after_erase();

        // Section D — EquipmentRegistry gaps
        test_eq_update();
        test_eq_create_subset();
        test_eq_clear();
        test_eq_data_access();
        test_eq_iterator();

        // Section E — EquipmentTagRegistry gaps
        test_tag_erase();
        test_tag_update();
        test_tag_create_subset();
        test_tag_clear();
        test_tag_data();
        test_tag_iterator();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
