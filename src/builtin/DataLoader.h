#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/Profile.h"
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class TagResolver;

namespace besq::data {

/// Load builtin enchantment and equipment data.
///
/// Tries filesystem first (allows user to replace builtin data), falls back
/// to data embedded in the binary.  Accepts explicit registry references
/// so both main.cpp (local instances) and benchmarks (singletons) can use it.
void load_builtin_data(TagRegistry& tag_reg,
                       EnchantmentRegistry& ench_reg,
                       EquipmentRegistry& eq_reg,
                       const std::filesystem::path& data_dir = "data/builtin");

/// Parse the builtin vanilla.json `tags` object into {key, values} pairs.
///
/// This is the SINGLE canonical extractor for the builtin tag definitions
/// (override-aware: filesystem `data/builtin/vanilla.json` wins over the
/// embedded resource).  Tag keys follow the "<ns>:<tagpath>" convention —
/// e.g. "minecraft:swords", "minecraft:enchantable/sharp_weapon" — and
/// values are the raw array entries (concrete IDs or `#`-references),
/// preserved verbatim so nested tag expansion happens lazily at resolution
/// time.  Both `DataLoader` (base_tags / resolver seeding) and the parser
/// (`seed_vanilla_tags`) route through this so embedded vs override never
/// diverge.  Results are cached per data_dir for the process lifetime.
std::vector<std::pair<std::string, std::vector<std::string>>>
load_builtin_tag_entries(const std::filesystem::path& data_dir = "data/builtin");

/// Build a TagResolver from the builtin vanilla.json `tags` object (real MC
/// item + enchantment tags).  Attach this to a Profile so the business→algorithm
/// boundary can compute an item's tag membership (`tags_of`) for applicability.
/// Same content source as load_builtin_data (filesystem override or embedded).
std::shared_ptr<TagResolver> make_builtin_tag_resolver(const std::filesystem::path& data_dir = "data/builtin");

/// Parse the builtin vanilla.json top-level metadata (name/description/author/
/// version/mc_version/display_name/parent/dependencies) into a ProfileMetadata.
/// Same content source as load_builtin_data (filesystem override or embedded);
/// `name` is the JSON root's human-readable name ("Vanilla"), NOT the profile
/// key — the caller decides the key (builtin:vanilla).  Timestamps default to
/// now (Profile construction guards zero time_points anyway).
ProfileMetadata load_builtin_metadata(const std::filesystem::path& data_dir = "data/builtin");

} // namespace besq::data
