#include "framework/test_utils.h"
#include "registries/CompactedRegistries.h"
#include "registries/RegistryAccess.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/ForgeConfig.h"
#include <stdexcept>

namespace {

void setup() {
    registries::categories().initialize();
    registries::enchants().initialize({
        {"sharpness", "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentCategory::ID_SWORD}},
        {"knockback", "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentCategory::ID_SWORD}},
        {"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
         1, false, {"sharpness"}, {EquipmentCategory::ID_SWORD}},
        {"protection", "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
    });
}

void test_basic_init_and_size() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategory::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // 4 enchantments total, but only 3 applicable to sword
    // Actually init() takes ALL from the registry, applicability is per-ench
    expect(reg.size() == 4, "size: should have 4 enchantments");
    expect(reg.get_target_equip().name_id == "diamond_sword",
           "target: should be diamond_sword");

    std::cout << "PASS: test_basic_init_and_size" << std::endl;
}

void test_safe_get_bounds() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategory::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // Valid access via .at() path
    expect(reg.get(0).mul > 0, "get(0): multiplier should be > 0");

    // Out-of-range access via .at() should throw
    bool threw = false;
    try {
        reg.get(static_cast<int16_t>(reg.size()));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on out-of-range id");

    // Negative id via .at() should throw
    threw = false;
    try {
        reg.get(static_cast<int16_t>(-1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on negative id");

    std::cout << "PASS: test_safe_get_bounds" << std::endl;
}

void test_conflict_detection() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategory::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // sharpness(0) and bane_of_arthropods(2) should conflict
    expect(reg.is_conflict(0, 2), "conflict: sharpness vs bane should conflict");
    expect(reg.is_conflict(2, 0), "conflict: bane vs sharpness should conflict (symmetric)");

    // sharpness(0) and knockback(1) should NOT conflict
    expect(!reg.is_conflict(0, 1), "conflict: sharpness vs knockback should NOT conflict");

    // Self-check should NOT conflict
    expect(!reg.is_conflict(0, 0), "conflict: self should NOT conflict");
    expect(!reg.is_conflict(1, 1), "conflict: self should NOT conflict");

    std::cout << "PASS: test_conflict_detection" << std::endl;
}

void test_multiplier_and_max_level() {
    registries::enchants().reset_for_testing();
    setup();

    Equipment sword{"diamond_sword", "Diamond Sword",
                    EquipmentCategory::ID_SWORD, 1561};
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    // sharpness: mult=1, max_lvl=5
    expect(reg.get_multiplier(0) == 1, "multiplier: sharpness should be 1");
    expect(reg.get_max_level(0) == 5, "max_level: sharpness should be 5");

    // knockback: mult=2, max_lvl=2
    expect(reg.get_multiplier(1) == 2, "multiplier: knockback should be 2");
    expect(reg.get_max_level(1) == 2, "max_level: knockback should be 2");

    std::cout << "PASS: test_multiplier_and_max_level" << std::endl;
}

// ─── compact::EnchSet dedicated tests ─────────────────────────────────────

void test_enchset_hash_consistency() {
    compact::EnchSet set;
    set.insert({0, 5});
    set.insert({1, 2});

    auto h1 = set.hash();
    auto h2 = set.hash();
    expect(h1 == h2, "enchset hash: same set should produce same hash");

    // Different levels should produce different hashes
    compact::EnchSet set2;
    set2.insert({0, 3});
    set2.insert({1, 2});
    auto h3 = set2.hash();
    expect(h1 != h3, "enchset hash: different levels should differ");

    // Different IDs should produce different hashes
    compact::EnchSet set3;
    set3.insert({0, 5});
    set3.insert({2, 2});
    auto h4 = set3.hash();
    expect(h1 != h4, "enchset hash: different ids should differ");

    std::cout << "PASS: test_enchset_hash_consistency" << std::endl;
}

void test_enchset_sort_restores_invariant() {
    compact::EnchSet set;
    set.insert({0, 5});
    set.insert({2, 3});

    // Mutate through mutable iterator to break sorting
    auto it = set.begin();
    std::swap(*it, *(it + 1));  // swap elements: now [id=2, id=0] — unsorted

    // find() should fail on unsorted data
    auto found = set.find(2);
    // With broken invariant, find may still work via binary search luck
    // but the real test is whether sort() fixes it
    set.sort();

    // After sort, find should work correctly
    auto after = set.find(2);
    expect(after != set.end() && after->level == 3,
           "enchset sort: find(2) should work after sort");
    auto after0 = set.find(0);
    expect(after0 != set.end() && after0->level == 5,
           "enchset sort: find(0) should work after sort");

    std::cout << "PASS: test_enchset_sort_restores_invariant" << std::endl;
}

void test_enchset_empty_and_single() {
    compact::EnchSet empty;
    expect(empty.size() == 0, "enchset empty: size 0");
    expect(empty.empty(), "enchset empty: empty() true");
    expect(empty.find(0) == empty.end(), "enchset empty: find returns end");

    compact::EnchSet single;
    single.insert({3, 1});
    expect(single.size() == 1, "enchset single: size 1");
    expect(single.contains(3), "enchset single: contains 3");
    expect(!single.contains(0), "enchset single: not contains 0");

    // Insert same id should update level (not grow)
    single.insert({3, 2});
    expect(single.size() == 1, "enchset single: update same id, size still 1");
    auto it = single.find(3);
    expect(it != single.end() && it->level == 2,
           "enchset single: find(3) level updated to 2");

    std::cout << "PASS: test_enchset_empty_and_single" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        test_basic_init_and_size();
        test_safe_get_bounds();
        test_conflict_detection();
        test_multiplier_and_max_level();
        test_enchset_hash_consistency();
        test_enchset_sort_restores_invariant();
        test_enchset_empty_and_single();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
