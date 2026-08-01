#include "ProfileLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "builtin/DataLoader.h"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// Load
// ============================================================================

Profile ProfileLoader::load(const std::filesystem::path& path) {
    Profile profile;
    load_into(profile, path);
    return profile;
}

bool ProfileLoader::load_into(Profile& profile, const std::filesystem::path& path) {
    try {
        auto [ench_data, eq_data] = FormatDetector::parse(path);

        RegistryLoader loader;
        TagRegistry tag_reg;
        EquipmentRegistry eq_reg;
        EnchantmentRegistry ench_reg;

        // Step 1: Build tag registry.  Seed with the builtin vanilla
        // categories first (vanilla fallback) so a mod profile's
        // `applicable_equipment` references resolve against vanilla
        // categories even when the profile itself does not define them
        // (e.g. a mod enchant targeting "sword"), then overlay the
        // profile's own equipment categories.
        {
            TagRegistry builtin_tags;
            EnchantmentRegistry builtin_ench;
            EquipmentRegistry builtin_eq;
            besq::data::load_builtin_data(builtin_tags, builtin_ench, builtin_eq);
            for (const auto& [id, tag] : builtin_tags.data())
                tag_reg.insert(tag);
        }
        {
            std::unordered_set<std::string> seen;
            for (const auto& eq : eq_data) {
                if (seen.insert(eq.category).second)
                    tag_reg.insert({NSID("#minecraft:" + eq.category), eq.category});
            }
        }

        // Step 2: Populate registries into temporary containers
        loader.from_dto(eq_reg, tag_reg, eq_data);
        loader.from_dto(ench_reg, tag_reg, ench_data);

        // Step 3: Construct Profile via full-parameter constructor
        std::string stem = path.stem().string();
        profile = Profile(ProfileMetadata(NSID(stem)), std::move(ench_reg), std::move(eq_reg), std::move(tag_reg));

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load profile from '%s': %s", path.string().c_str(), e.what());
        return false;
    }
}

Profile ProfileLoader::from_json(const Json& json) {
    Profile profile;
    from_json(profile, json);
    return profile;
}

bool ProfileLoader::from_json(Profile& profile, const Json& json) {
    try {
        profile = Profile::from_json_static(json);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse profile from JSON: %s", e.what());
        return false;
    }
}

Profile ProfileLoader::load_builtin() {
    Profile profile;
    load_builtin(profile);
    return profile;
}

bool ProfileLoader::load_builtin(Profile& profile) {
    try {
        TagRegistry tag_reg;
        EnchantmentRegistry ench_reg;
        EquipmentRegistry eq_reg;
        besq::data::load_builtin_data(tag_reg, ench_reg, eq_reg);
        profile = Profile(ProfileMetadata(NSID("builtin:vanilla")), std::move(ench_reg),
                          std::move(eq_reg), std::move(tag_reg));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load built-in data: %s", e.what());
        return false;
    }
}

// ============================================================================
// Save
// ============================================================================

Json ProfileLoader::to_json(const Profile& profile) {
    return profile.to_json();
}

std::string ProfileLoader::to_json_string(const Profile& profile) {
    return profile.to_json().to_string(Json::Pretty);
}

bool ProfileLoader::save(const Profile& profile, const std::filesystem::path& path) {
    try {
        std::string json_str = to_json_string(profile);
        std::ofstream out(path);
        if (!out) {
            LOG_ERROR("Failed to open '%s' for writing", path.string().c_str());
            return false;
        }
        out << json_str;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save profile to '%s': %s", path.string().c_str(), e.what());
        return false;
    }
}
