#define BESQ_TEST_MAIN
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "framework/test_framework.h"
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper: create a simple valid enchantment list
// ---------------------------------------------------------------------------
std::vector<EnchInfo> make_valid_enchants() {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("minecraft:sharpness"), "Sharpness", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:smite"), "Smite", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    return infos;
}

// ---------------------------------------------------------------------------
// test_initialize_and_get
// ---------------------------------------------------------------------------
TEST_CASE("test_initialize_and_get") {
    auto infos = make_valid_enchants();
    EnchantmentRegistry reg(infos);

    expect(reg.size() == 2, "should have 2 enchantments");

    // Get by NSID
    const auto& s0 = reg.at(NSID("minecraft:sharpness"));
    expect(s0.id.str() == "minecraft:sharpness", "at(sharpness) id matches");

    const auto& s1 = reg.at(NSID("minecraft:smite"));
    expect(s1.id.str() == "minecraft:smite", "at(smite) id matches");

    // find() returns iterator or end()
    auto it = reg.find(NSID("minecraft:sharpness"));
    expect(it != reg.end(), "find(sharpness) should be found");
    expect(it->max_level == 5, "find(sharpness)->max_level");

    std::cout << "PASS: test_initialize_and_get" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_bounds
// ---------------------------------------------------------------------------
TEST_CASE("test_get_bounds") {
    auto infos = make_valid_enchants();
    EnchantmentRegistry reg(infos);

    // Unknown NSID via at() — throws out_of_range
    bool threw = false;
    try {
        reg.at(NSID("unknown_ench"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "at(NSID(\"unknown\")) should throw out_of_range");

    // contains for unknown
    expect(!reg.contains(NSID("nonexistent")), "contains(\"nonexistent\") == false");

    // contains for existing
    expect(reg.contains(NSID("minecraft:sharpness")), "contains(\"sharpness\") == true");

    std::cout << "PASS: test_get_bounds" << std::endl;
}

// ---------------------------------------------------------------------------
// test_check_validation
// ---------------------------------------------------------------------------
TEST_CASE("test_check_validation") {
    // Valid data
    auto valid = make_valid_enchants();
    expect(EnchantmentRegistry::check_validation(valid), "valid data passes validation");

    // Empty NSID
    std::vector<EnchInfo> bad_name;
    bad_name.emplace_back(NSID(), "Empty", MCE::Java, 1, 1, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{});
    expect(!EnchantmentRegistry::check_validation(bad_name), "empty NSID should fail validation");

    // max_level <= 0
    std::vector<EnchInfo> bad_max;
    bad_max.emplace_back(NSID("test"), "Test", MCE::Java, 0, 0, 1, false, std::unordered_set<NSID>{},
                         std::unordered_set<NSID>{});
    expect(!EnchantmentRegistry::check_validation(bad_max), "max_level <= 0 should fail");

    // multiplier <= 0
    std::vector<EnchInfo> bad_mult;
    bad_mult.emplace_back(NSID("test"), "Test", MCE::Java, 1, 1, 0, false, std::unordered_set<NSID>{},
                          std::unordered_set<NSID>{});
    expect(!EnchantmentRegistry::check_validation(bad_mult), "multiplier <= 0 should fail");

    // limited_level > max_level
    std::vector<EnchInfo> bad_limited;
    bad_limited.emplace_back(NSID("test"), "Test", MCE::Java, 1, 5, 1, false, std::unordered_set<NSID>{},
                             std::unordered_set<NSID>{});
    expect(!EnchantmentRegistry::check_validation(bad_limited), "limited > max should fail");

    // exclusive_set references non-existent enchantment
    std::vector<EnchInfo> bad_excl;
    bad_excl.emplace_back(NSID("test"), "Test", MCE::Java, 1, 1, 1, false, std::unordered_set<NSID>{NSID("nonexistent_ench")},
                          std::unordered_set<NSID>{});
    expect(!EnchantmentRegistry::check_validation(bad_excl), "bad exclusive ref should fail");

    std::cout << "PASS: test_check_validation" << std::endl;
}

// ---------------------------------------------------------------------------
// test_is_incompatible
// ---------------------------------------------------------------------------
TEST_CASE("test_is_incompatible") {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("sharpness"), "Sharpness", MCE::Java, 5, 5, 1, false,
                       std::unordered_set<NSID>{NSID("smite"), NSID("bane_of_arthropods")}, std::unordered_set<NSID>{});
    infos.emplace_back(NSID("smite"), "Smite", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{NSID("sharpness")},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("unbreaking"), "Unbreaking", MCE::Java, 3, 3, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);

    // Use NSID-based lookup instead of numeric index
    auto sharp_it = reg.find(NSID("sharpness"));
    auto smite_it = reg.find(NSID("smite"));
    auto bane_it = reg.find(NSID("bane_of_arthropods"));
    auto ub_it = reg.find(NSID("unbreaking"));

    expect(sharp_it != reg.end(), "sharpness should be in registry");
    expect(smite_it != reg.end(), "smite should be in registry");
    expect(bane_it != reg.end(), "bane should be in registry");
    expect(ub_it != reg.end(), "unbreaking should be in registry");

    expect(reg.is_incompatible(sharp_it->id, smite_it->id), "sharpness and smite are incompatible");
    expect(reg.is_incompatible(smite_it->id, sharp_it->id), "smite and sharpness are incompatible (symmetric)");

    // sharpness and bane_of_arthropods are incompatible (sharpness lists it)
    expect(reg.is_incompatible(sharp_it->id, bane_it->id), "sharpness incompatible with bane_of_arthropods");
    expect(reg.is_incompatible(bane_it->id, sharp_it->id), "bane_of_arthropods incompatible with sharpness");

    // smite and bane should NOT be incompatible (no mutual exclusivity defined)
    expect(!reg.is_incompatible(smite_it->id, bane_it->id), "smite and bane are compatible");

    // unbreaking is compatible with everything
    expect(!reg.is_incompatible(ub_it->id, sharp_it->id), "unbreaking compatible with sharpness");
    expect(!reg.is_incompatible(ub_it->id, smite_it->id), "unbreaking compatible with smite");
    expect(!reg.is_incompatible(ub_it->id, bane_it->id), "unbreaking compatible with bane");

    // Same enchantment is never incompatible with itself
    expect(!reg.is_incompatible(sharp_it->id, sharp_it->id), "same ench is never incompatible");

    std::cout << "PASS: test_is_incompatible" << std::endl;
}

// ---------------------------------------------------------------------------
// test_exclusive_set_access
// ---------------------------------------------------------------------------
TEST_CASE("test_exclusive_set_access") {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("sharpness"), "Sharpness", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{NSID("smite")},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("smite"), "Smite", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{NSID("sharpness")},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);

    const auto& excl = reg.get_exclusive_set(NSID("sharpness"));
    expect(excl.size() == 1, "exclusive_set(sharpness) should have 1 entry");
    expect(excl.contains(NSID("smite")), "exclusive_set(sharpness) should contain smite");

    // Enchantment with no incompatibilities
    const auto& empty = reg.get_exclusive_set(NSID("nonexistent"));
    expect(empty.empty(), "exclusive_set for unknown NSID should be empty");

    std::cout << "PASS: test_exclusive_set_access" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------