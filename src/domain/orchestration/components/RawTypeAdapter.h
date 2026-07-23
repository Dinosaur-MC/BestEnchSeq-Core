#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include "domain/interface/types/RawTypes.h"
#include <vector>

/// Converts raw (string-based) intermediate types to/from domain registries.
///
/// This is the bridge between parser string-based output and the domain type
/// system (int32_t IDs), handling category deduplication and cross-referencing
/// internally.  Also provides the reverse direction (domain -> raw) for
/// serialization or round-trip scenarios.
struct RawTypeAdapter {

    // ── Raw → domain ────────────────────────────────────────────────────────

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
        EquipmentTagRegistry& tag_reg,
        EquipmentRegistry& eq_reg,
        EnchantmentRegistry& ench_reg);

    /// Resolve a vector of raw enchantment info into domain EnchInfo objects.
    /// Category name strings are converted to int32_t IDs via cat_reg.
    static std::vector<EnchInfo> resolve_ench_info(
        const std::vector<RawEnchantment>& raw,
        const EquipmentTagRegistry& tag_reg);

    /// Resolve a vector of raw equipment info into domain Equipment objects.
    /// Category name strings are converted to int32_t IDs via cat_reg.
    /// Unknown category names map to EquipmentCategory::ID_ANY.
    static std::vector<Equipment> resolve_equipment(
        const std::vector<RawEquipment>& raw,
        const EquipmentTagRegistry& tag_reg);

    // ── Domain → raw ────────────────────────────────────────────────────────

    /// Convert registry contents back to raw intermediate types.
    /// Requires the EquipmentTagRegistry for tag -> name resolution.
    static void revert(
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& eq_reg,
        const EquipmentTagRegistry& tag_reg,
        std::vector<RawEnchantment>& out_enchants,
        std::vector<RawEquipment>& out_equipments);
};
