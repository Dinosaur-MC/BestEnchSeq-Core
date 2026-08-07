#define BESQ_TEST_MAIN

#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "framework/test_framework.h"

#include <iostream>

namespace {

TEST_CASE("test_registry_contains_bare") {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    expect(reg.contains(NSID("sharpness")), "bare name 'sharpness' should be found");

    expect(!reg.contains(NSID("nonexistent")), "unknown name should not be found");

    std::cout << "  PASS: test_registry_contains_bare" << std::endl;
}

TEST_CASE("test_registry_contains_namespaced") {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    // Bare lookup — NSID("sharpness") normalizes to "minecraft:sharpness"
    expect(reg.contains(NSID("sharpness")), "bare name should resolve to namespaced entry");

    // Full namespaced lookup
    expect(reg.contains(NSID("minecraft:sharpness")), "ns:id should resolve");

    // Unknown enchantment
    expect(!reg.contains(NSID("mod:unknown")), "unknown ns:id should not be found");

    std::cout << "  PASS: test_registry_contains_namespaced" << std::endl;
}

} // anonymous namespace
