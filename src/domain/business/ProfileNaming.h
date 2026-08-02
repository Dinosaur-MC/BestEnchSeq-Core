#pragma once
#include <filesystem>
#include <string>

/// Derive a datapack profile name, returned VERBATIM (B-T13: profile keys are
/// plain std::string — spaces/dots and any other characters are kept as-is, no
/// NSID charset cleanup, no leading-digit guard).
///
/// Precedence (B-T14 M-4): the FOLDER STEM wins; `pack.id` (typically a UUID)
/// is used only as a fallback when the folder has no usable stem.  An empty
/// name defaults to "datapack"; a name equal to "builtin:vanilla" (the
/// injected root key; "vanilla" kept as a legacy alias) is disambiguated to
/// "vanilla_datapack" so a datapack can never replace the injected vanilla
/// base profile.
std::string derive_datapack_name(const std::filesystem::path& dir);
