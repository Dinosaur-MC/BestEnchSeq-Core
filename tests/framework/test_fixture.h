#pragma once

#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/algorithm/registries/AlgorithmRegistry.h"
#include <cstdint>
#include <string>
#include <vector>

using algorithm::AlgorithmRegistry;

// ─── Test fixture: owns local registry instances  ──────────────────
//
// Replaces the old registries::enchants() / categories() / equipment()
// singleton accessors (RegistryAccess.h) with local instances.
//
// Usage:
//   TestFixture fx;
//   fx.categories.initialize();
//   fx.enchants.initialize({...});
//   // then pass fx.enchants / fx.categories / fx.equipment to code under test

struct TestFixture {
    EnchantmentRegistry enchants;
    EquipmentCategoryRegistry categories;
    EquipmentRegistry equipment;
    AlgorithmRegistry algorithms;

    TestFixture() = default;

    // Convenience: initialize with default builtin-like data
    void init_standard();
    void init_sword_set();
    void init_chestplate_set();
};

inline void TestFixture::init_sword_set() {
    categories.initialize();
    enchants.initialize({
        {"sharpness", "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentCategory::ID_SWORD}},
        {"knockback", "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentCategory::ID_SWORD}},
        {"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
         1, false, {"sharpness"}, {EquipmentCategory::ID_SWORD}},
        {"protection", "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
    });
    equipment.initialize({
        {"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561},
        {"diamond_chestplate", "Diamond Chestplate",
         EquipmentCategory::ID_CHESTPLATE, 528},
    });
}

inline void TestFixture::init_chestplate_set() {
    categories.initialize();
    enchants.initialize({
        {"protection", "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
        {"unbreaking", "Unbreaking", MCE::All, 3, 3,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
        {"thorns", "Thorns", MCE::All, 3, 3,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
    });
    equipment.initialize({
        {"diamond_chestplate", "Diamond Chestplate",
         EquipmentCategory::ID_CHESTPLATE, 528},
    });
}
