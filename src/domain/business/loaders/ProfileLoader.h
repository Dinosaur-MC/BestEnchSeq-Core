#pragma once
#include "domain/business/types/Profile.h"
#include <filesystem>
#include <string>

/// Primary external entry point for loading and saving Profiles.
///
/// All I/O operations take or return Profile as the unit of data.
/// Handles format detection, parsing, and registry population internally.
class ProfileLoader {
public:
    // ── Load ───────────────────────────────────────────────────────────

    /// Load from file → Profile (auto-detect format via FormatDetector).
    Profile load(const std::filesystem::path& path);

    /// Load into an existing Profile reference.
    bool load_into(Profile& profile, const std::filesystem::path& path);

    /// Load from JSON DOM → Profile.
    Profile from_json(const Json& json);

    /// Load into an existing Profile from JSON.
    bool from_json(Profile& profile, const Json& json);

    /// Load built-in vanilla data (delegates to project-level builtin/DataLoader).
    Profile load_builtin();

    /// Load built-in vanilla data into an existing Profile.
    bool load_builtin(Profile& profile);

    // ── Save ───────────────────────────────────────────────────────────

    /// Profile → JSON DOM.
    Json to_json(const Profile& profile);

    /// Profile → JSON string.
    std::string to_json_string(const Profile& profile);

    /// Profile → file.
    bool save(const Profile& profile, const std::filesystem::path& path);
};
