#define BESQ_TEST_MAIN
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "framework/test_framework.h"
#include <iostream>

namespace {

TEST_CASE("test_add_new_enchantment") {
    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    EnchantmentRegistry reg({sharp});

    EnchInfo new_ench{NSID("minecraft:custom_ench"), "Custom", MCE::All, 3, 3, 2, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    bool ok = reg.insert(new_ench).second;
    expect(ok, "insert should succeed for new enchantment");
    expect(reg.size() == 2, "registry should have 2 entries after insert");
    expect(reg.contains(NSID("minecraft:custom_ench")), "new enchantment should be findable");
    TEST_PASS("test_add_new_enchantment");
}

TEST_CASE("test_add_duplicate_fails") {
    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    EnchantmentRegistry reg({sharp});

    bool ok = reg.insert(sharp).second;
    expect(!ok, "insert should fail for duplicate name_id");
    TEST_PASS("test_add_duplicate_fails");
}

TEST_CASE("test_remove_existing") {
    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    EnchantmentRegistry reg({sharp});

    bool ok = reg.erase(NSID("minecraft:sharpness"));
    expect(ok, "erase should succeed for existing entry");
    expect(!reg.contains(NSID("minecraft:sharpness")), "removed entry should not be findable");
    TEST_PASS("test_remove_existing");
}

TEST_CASE("test_remove_nonexistent_fails") {
    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    EnchantmentRegistry reg({sharp});

    bool ok = reg.erase(NSID("minecraft:nonexistent"));
    expect(!ok, "erase should fail for nonexistent entry");
    TEST_PASS("test_remove_nonexistent_fails");
}

TEST_CASE("test_modify_max_level") {
    EnchantmentRegistry reg;
    EnchInfo sharp{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    reg = EnchantmentRegistry({sharp});

    auto modified = reg.at(NSID("minecraft:sharpness"));
    EnchInfo patch = modified;
    patch.max_level = 10;
    bool ok = reg.update(patch);
    expect(ok, "update should succeed");

    const auto& updated = reg.at(NSID("minecraft:sharpness"));
    expect(updated.max_level == 10, "max_level should be updated to 10");
    expect(updated.multiplier == 1, "multiplier should remain unchanged");
    TEST_PASS("test_modify_max_level");
}

TEST_CASE("test_modify_nonexistent_fails") {
    EnchantmentRegistry reg;
    EnchInfo nonexistent{NSID("minecraft:nonexistent"), "Nonexistent", MCE::All, 1, 1, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
    bool ok = reg.update(nonexistent);
    expect(!ok, "update should fail for nonexistent entry");
    TEST_PASS("test_modify_nonexistent_fails");
}

} // namespace
