#include "ProfileLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "builtin/DataLoader.h"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
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
        EquipmentTagRegistry tag_reg;

        // Step 1: Build tag registry from equipment categories
        {
            std::unordered_set<std::string> seen;
            for (const auto& eq : eq_data) {
                if (seen.insert(eq.category).second)
                    tag_reg.insert({NSID("#minecraft:" + eq.category), eq.category});
            }
        }

        // Step 2: Populate equipment
        loader.from_dto(profile._eq, tag_reg, eq_data);

        // Step 3: Populate enchantments
        loader.from_dto(profile._ench, tag_reg, ench_data);

        // Step 4: Copy tags
        profile._tags = std::move(tag_reg);

        // Set metadata from filename
        std::string stem = path.stem().string();
        profile._meta.name = NSID(stem);
        profile._meta.created_at = std::chrono::system_clock::now();
        profile._meta.updated_at = profile._meta.created_at;

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
        profile = Profile::from_json(json);
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
        besq::data::load_builtin_data(
            profile._tags, profile._ench, profile._eq
        );
        if (profile.name().empty()) {
            profile._meta.name = NSID("builtin:vanilla");
        }
        profile._meta.created_at = std::chrono::system_clock::now();
        profile._meta.updated_at = profile._meta.created_at;
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
