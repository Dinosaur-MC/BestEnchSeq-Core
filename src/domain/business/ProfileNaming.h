#pragma once
#include <filesystem>
#include <string>

/// Derive a datapack profile name from `dir/pack.mcmeta` `pack.id` (else the
/// directory stem), returned VERBATIM (B-T13: profile keys are plain
/// std::string — spaces/dots and any other characters are kept as-is, no
/// NSID charset cleanup, no leading-digit guard).  An empty (no-stem)
/// directory defaults to "datapack"; a name equal to "builtin:vanilla" (the
/// injected root key; "vanilla" kept as a legacy alias) is disambiguated to
/// "vanilla_datapack" so a datapack can never replace the injected vanilla
/// base profile.
std::string derive_datapack_name(const std::filesystem::path& dir);
