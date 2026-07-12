#pragma once

#include "types/Ench.h"
#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include "types/RawTypes.h"
#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentCategoryRegistry;

/// Converts string-based intermediate data (RawEnchInfo / RawEquipment) into
/// resolved domain types (EnchInfo / Equipment) by looking up category names
/// against the EquipmentCategoryRegistry.
///
/// Also provides name-to-ID resolution for enchantment lookup, centralizing
/// the fallback logic (trying bare name first, then "minecraft:" prefix).
///
/// This is the explicit resolution step in the parse → resolve → initialize
/// pipeline, separating string-based parsing from registry-aware resolution.
struct RegistryResolver {

    // ── Raw → domain type resolution ────────────────────────────────────

    /// Resolve a vector of raw enchantment info into domain EnchInfo objects.
    /// Category name strings are converted to int32_t IDs via cat_reg.
    static std::vector<EnchInfo> resolve_ench_info(
        const std::vector<RawEnchInfo> &raw,
        const EquipmentCategoryRegistry &cat_reg
    );

    /// Resolve a vector of raw equipment info into domain Equipment objects.
    /// Category name strings are converted to int32_t IDs via cat_reg.
    /// Unknown category names map to EquipmentCategory::ID_ANY.
    static std::vector<Equipment> resolve_equipment(
        const std::vector<RawEquipment> &raw,
        const EquipmentCategoryRegistry &cat_reg
    );

    // ── Enchantment name resolution ─────────────────────────────────────

    /// Resolve a plain enchantment name to an int32_t ID.
    /// Tries the raw name first, then prepends "minecraft:" as fallback.
    /// Returns -1 if not found.
    static int32_t resolve_ench_id(
        const std::string &name,
        const EnchantmentRegistry &ench_reg
    );

    /// Resolve a namespaced enchantment spec to an int32_t ID.
    /// Constructs "ns:id" unless the id already contains a namespace colon.
    /// Falls back to bare id if namespaced lookup fails.
    /// Throws std::runtime_error if not found.
    static int32_t resolve_ench_id(
        const std::string &ns,
        const std::string &id,
        const EnchantmentRegistry &ench_reg
    );

    // ── Raw data merging (multi-source data pack support) ──────────────

    /// Merge new raw enchantments into an existing vector.
    /// Entries from \p extra whose name_id does not already appear in
    /// \p base are appended.  This avoids duplicate resolution work when
    /// loading multiple data packs.
    static void merge_raw_ench_info(
        std::vector<RawEnchInfo> &base,
        const std::vector<RawEnchInfo> &extra
    );

    /// Merge new raw equipment into an existing vector (same dedup logic).
    static void merge_raw_equipment(
        std::vector<RawEquipment> &base,
        const std::vector<RawEquipment> &extra
    );
};
