#include "ProfileLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "builtin/DataLoader.h"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

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
        // Phase 1: parse the profile's own DTOs.  The vanilla universe is
        // loaded separately below as the cross-validation fallback.
        auto [ench_data, eq_data] = FormatDetector::parse(path);

        // Phase 2: two-phase loading — build the vanilla universe into
        // TEMPORARY registries (tags + equipment + enchantments), then
        // cross-validate the profile's DTOs on top of the union.  A user
        // Profile must NOT contain vanilla's registries as its own, so after
        // validation the Profile keeps ONLY its own enchantments/equipments;
        // the vanilla tag universe is retained so the profile's `#tag`
        // supported_items references stay interpretable downstream.
        RegistryLoader loader;
        TagRegistry tag_reg;          // vanilla universe: tags
        EquipmentRegistry eq_reg;     // vanilla universe: equipment
        EnchantmentRegistry ench_reg; // vanilla universe + profile content
        besq::data::load_builtin_data(tag_reg, ench_reg, eq_reg);
        loader.resolve_with_base(ench_data, eq_data, tag_reg, eq_reg, ench_reg);

        // Filter the union back to the profile's own content (ids come from
        // the profile's raw DTOs; NSID() normalization matches from_dto).
        std::unordered_set<NSID> profile_ench_ids;
        for (const auto& d : ench_data)
            profile_ench_ids.insert(NSID(d.id));
        std::unordered_set<NSID> profile_eq_ids;
        for (const auto& d : eq_data)
            profile_eq_ids.insert(NSID(d.id));

        EnchantmentRegistry profile_ench;
        for (const auto& [id, info] : ench_reg.data())
            if (profile_ench_ids.count(id) != 0)
                profile_ench.insert(info);
        EquipmentRegistry profile_eq;
        for (const auto& [id, eq] : eq_reg.data())
            if (profile_eq_ids.count(id) != 0)
                profile_eq.insert(eq);

        // Construct Profile via full-parameter constructor.
        std::string stem = path.stem().string();
        profile = Profile(ProfileMetadata(NSID(stem)), std::move(profile_ench),
                          std::move(profile_eq), std::move(tag_reg));
        // Attach the vanilla tag universe resolver so the profile's `#tag`
        // supported_items references resolve at the business→algorithm boundary
        // (T7/T10: real MC item tags are the applicability source of truth).
        profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());

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
        profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());
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
