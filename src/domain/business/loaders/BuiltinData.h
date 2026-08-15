#pragma once
#include "common/utils/ExeDir.hpp"
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

/// Compile-time-only relative data directory for the runtime disk-override
/// check: `<exe_dir>/<BESQ_BUILD_DATA_DIR>/vanilla.json` overrides the
/// embedded copy when present.  Defined by CMake (read-only build config —
/// NOT configurable at runtime; see the exe-dir defaults design).  Falls back
/// to the CWD-relative "data/builtin" when exe_dir() is unavailable.
#ifndef BESQ_BUILD_DATA_DIR
#define BESQ_BUILD_DATA_DIR "data/builtin"
#endif

/// Default runtime override directory (exe-relative; empty exe_dir → "data/builtin").
inline std::filesystem::path default_data_dir() {
    const auto exe = exe_dir();
    return exe.empty() ? std::filesystem::path("data/builtin")
                       : exe / BESQ_BUILD_DATA_DIR;
}

/// Load builtin enchantment and equipment data.
///
/// Tries the embedded vanilla.json first (via raw(ResourceId::data_vanilla_json)),
/// then a filesystem override at `data_dir` when present (allows users to
/// replace builtin data).  Accepts explicit registry references so both the
/// profile bootstrap (ProfileLoader) and the two-phase validation universe
/// (RegistryLoader) can use it.
void load_builtin_data(TagRegistry& tag_reg,
                       EnchantmentRegistry& ench_reg,
                       EquipmentRegistry& eq_reg,
                       const std::filesystem::path& data_dir = default_data_dir());

/// Parse the builtin vanilla.json `tags` object into {key, values} pairs.
///
/// This is the SINGLE canonical extractor for the builtin tag definitions
/// (override-aware: filesystem `data/builtin/vanilla.json` wins over the
/// embedded resource).  Tag keys follow the "<ns>:<tagpath>" convention —
/// e.g. "minecraft:swords", "minecraft:enchantable/sharp_weapon" — and
/// values are the raw array entries (concrete IDs or `#`-references),
/// preserved verbatim so nested tag expansion happens lazily at resolution
/// time.  Both `load_builtin_data` (base_tags / resolver seeding) and the
/// parsers (`seed_vanilla_tags`) route through this so embedded vs override
/// never diverge.  Results are cached per data_dir for the process lifetime.
std::vector<std::pair<std::string, std::vector<std::string>>>
load_builtin_tag_entries(const std::filesystem::path& data_dir = default_data_dir());

/// Build a TagResolver from the builtin vanilla.json `tags` object (real MC
/// item + enchantment tags).  Attach this to a Profile so the business→algorithm
/// boundary can compute an item's tag membership (`tags_of`) for applicability.
/// Same content source as load_builtin_data (filesystem override or embedded).
std::shared_ptr<TagResolver> make_builtin_tag_resolver(const std::filesystem::path& data_dir = default_data_dir());

/// Parse the builtin vanilla.json top-level metadata (name/description/author/
/// version/mc_version/display_name/parent/dependencies) into a ProfileMetadata.
/// Same content source as load_builtin_data (filesystem override or embedded);
/// `name` is the JSON root's human-readable name ("Vanilla"), NOT the profile
/// key — the caller decides the key (builtin:vanilla).  Timestamps default to
/// now (Profile construction guards zero time_points anyway).
ProfileMetadata load_builtin_metadata(const std::filesystem::path& data_dir = default_data_dir());

} // namespace besq::data
