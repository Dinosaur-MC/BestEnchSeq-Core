#pragma once
#include "business-domain/registries/EnchantmentRegistry.h"
#include "business-domain/registries/EquipmentCategoryRegistry.h"
#include "business-domain/registries/EquipmentRegistry.h"
#include <filesystem>

namespace besq::data {

/// Load builtin enchantment and equipment data.
///
/// Tries filesystem first (allows user to replace builtin data), falls back
/// to data embedded in the binary.  Accepts explicit registry references
/// so both main.cpp (local instances) and benchmarks (singletons) can use it.
void load_builtin_data(
    EquipmentCategoryRegistry& cat_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir = "data/builtin"
);

} // namespace besq::data
