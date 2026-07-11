#pragma once

#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include "types/RawTypes.h"
#include <vector>

class EquipmentCategoryRegistry;

/// Converts string-based intermediate data (RawEnchInfo / RawEquipment) into
/// resolved domain types (EnchInfo / Equipment) by looking up category names
/// against the EquipmentCategoryRegistry.
///
/// This is the explicit resolution step in the parse → resolve → initialize
/// pipeline, separating string-based parsing from registry-aware resolution.
struct RegistryResolver {

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
};
