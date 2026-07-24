#pragma once

#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/registries/AlgorithmRegistry.h"
#include <vector>

using algorithm::AlgorithmRegistry;

// ─── Test fixture: owns local registry instances  ──────────────────
//
// Replaces the old registries::enchants() / categories() / equipment()
// singleton accessors (RegistryAccess.h) with local instances.
//
// Usage:
//   TestFixture fx;
//   fx.init_sword_set();
//   // then pass fx.enchants / fx.categories / fx.equipment to code under test

struct TestFixture {
    EnchantmentRegistry enchants;
    EquipmentTagRegistry categories;
    EquipmentRegistry equipment;
    AlgorithmRegistry algorithms;

    TestFixture() = default;

    // Convenience: initialize with default builtin-like data
    void init_standard();
    void init_sword_set();
    void init_chestplate_set();
};

inline void TestFixture::init_sword_set() {
    categories = EquipmentTagRegistry({
        {EquipmentTag::sword(), "sword"},
        {EquipmentTag::chestplate(), "chestplate"},
    });
    enchants = EnchantmentRegistry({
        EnchInfo{NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
         1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::sword()}},
        EnchInfo{NSID("knockback"), "Knockback", MCE::All, 2, 2,
         2, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::sword()}},
        EnchInfo{NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::All, 5, 5,
         1, false, std::unordered_set<NSID>{NSID("sharpness")}, std::unordered_set<NSID>{EquipmentTag::sword()}},
        EnchInfo{NSID("protection"), "Protection", MCE::All, 4, 4,
         1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::chestplate()}},
    });
    equipment = EquipmentRegistry({
        {NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561},
        {NSID("minecraft:diamond_chestplate"), "Diamond Chestplate",
         EquipmentTag::chestplate(), 528},
    });
}

inline void TestFixture::init_chestplate_set() {
    categories = EquipmentTagRegistry({
        {EquipmentTag::chestplate(), "chestplate"},
    });
    enchants = EnchantmentRegistry({
        EnchInfo{NSID("protection"), "Protection", MCE::All, 4, 4,
         1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::chestplate()}},
        EnchInfo{NSID("unbreaking"), "Unbreaking", MCE::All, 3, 3,
         1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::chestplate()}},
        EnchInfo{NSID("thorns"), "Thorns", MCE::All, 3, 3,
         1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{EquipmentTag::chestplate()}},
    });
    equipment = EquipmentRegistry({
        {NSID("minecraft:diamond_chestplate"), "Diamond Chestplate",
         EquipmentTag::chestplate(), 528},
    });
}
