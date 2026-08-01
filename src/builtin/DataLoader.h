#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include <filesystem>
#include <memory>

class TagResolver;

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

/// Build a TagResolver from the builtin vanilla.json `tags` object (real MC
/// item + enchantment tags).  Attach this to a Profile so the business→algorithm
/// boundary can compute an item's tag membership (`tags_of`) for applicability.
/// Same content source as load_builtin_data (filesystem override or embedded).
std::shared_ptr<TagResolver> make_builtin_tag_resolver(
    const std::filesystem::path& data_dir = "data/builtin"
);

} // namespace besq::data
