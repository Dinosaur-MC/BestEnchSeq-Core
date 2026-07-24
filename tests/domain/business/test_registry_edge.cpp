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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
