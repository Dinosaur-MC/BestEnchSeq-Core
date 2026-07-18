#pragma once
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/RawTypes.h"
#include <vector>

/// Converts pre-resolved RawEnchantment[] + RawEquipment[] into domain
/// registries.  This is the bridge between parser string-based output and
/// the domain type system (int32_t IDs).
///
/// Unlike calling RegistryResolver methods directly, RawTypeAdapter owns the
/// full resolution pipeline: it builds and initializes all three registries
/// (EquipmentCategoryRegistry, EquipmentRegistry, EnchantmentRegistry) in one
/// call, handling category deduplication and cross-referencing internally.
struct RawTypeAdapter {

    /// Convert RawEnchantment[] + RawEquipment[] into domain registries.
    ///
    /// Fills the provided registry references with resolved data:
    ///   Step 1 — Build EquipmentCategoryRegistry from all unique category
    ///            names found in \p equipments.
    ///   Step 2 — Build EquipmentRegistry by resolving each RawEquipment's
    ///            category string to an int32_t ID via the category registry.
    ///   Step 3 — Build EnchantmentRegistry by resolving applicable_items
    ///            strings to category IDs and namespace-qualifying
    ///            exclusive_set entries.
    ///
    /// Throws std::runtime_error on validation failure (propagated from
    /// EnchantmentRegistry::initialize).
    static void resolve(
        const std::vector<RawEnchantment>& enchants,
        const std::vector<RawEquipment>& equipments,
        EquipmentCategoryRegistry& cat_reg,
        EquipmentRegistry& eq_reg,
        EnchantmentRegistry& ench_reg);
};
