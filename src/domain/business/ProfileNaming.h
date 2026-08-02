#pragma once
#include <filesystem>
#include <string>

/// Sanitize a raw name into a valid NSID name component.
///
/// NSID validation only allows `[A-Za-z0-9_-/]` and rejects a leading digit
/// (see common/CommonTypes.cpp validate_id).  Characters outside the allowed
/// set are replaced with `_`; an empty (or fully-invalid) name defaults to
/// "datapack".
std::string sanitize_nsid_name(std::string raw);

/// Derive a datapack profile name from `dir/pack.mcmeta` `pack.id` (else the
/// directory stem), sanitized into a valid NSID name component.  A name equal
/// to "vanilla" is disambiguated to "vanilla_datapack" so a datapack can never
/// replace the injected vanilla base profile.
std::string derive_datapack_name(const std::filesystem::path& dir);
