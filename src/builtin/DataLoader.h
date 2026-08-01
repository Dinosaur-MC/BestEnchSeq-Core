#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include <filesystem>
#include <vector>

namespace besq::data {

/// Load builtin enchantment and equipment data.
///
/// Tries filesystem first (allows user to replace builtin data), falls back
/// to data embedded in the binary.  Accepts explicit registry references
/// so both main.cpp (local instances) and benchmarks (singletons) can use it.
void load_builtin_data(
    TagRegistry& tag_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir = "data/builtin"
);

/// Parse the builtin vanilla data into raw DTOs (ench/eq). Used as the
/// validation universe (fallback) when cross-validating other profiles.
void load_builtin_dtos(
    std::vector<business::loader::EnchantmentData>& ench,
    std::vector<business::loader::EquipmentData>& eq);

} // namespace besq::data
