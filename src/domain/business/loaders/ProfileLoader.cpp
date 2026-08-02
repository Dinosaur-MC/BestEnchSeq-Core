#include "ProfileLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/components/LimitedLevelCalculator.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "builtin/DataLoader.h"
#include "builtin/ItemProperties.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

        // Phase 1b: parse the profile's declared dependencies from the raw
        // JSON root. FormatDetector::Result only carries enchantments and
        // equipments, so re-read the top-level `dependencies` array here.
        // (CSV / MC-official formats have no JSON dependencies array.)
        std::vector<std::string> dependencies;
        const auto format = FormatDetector::detect(path);
        if (format == DataFormat::NativeJson || format == DataFormat::Unknown) {
            auto root = Json::parse(file_utils::read_file(path));
            if (root.has(std::string(ProfileMetadata::KEY_DEPENDENCIES))) {
                Json dep_val = root[std::string(ProfileMetadata::KEY_DEPENDENCIES)];
                if (dep_val.type() == JsonType::Array) {
                    for (const auto& e : dep_val.as_array())
                        dependencies.push_back(e.as<std::string>());
                }
            }
        }

        // Phase 2: two-phase loading — build the vanilla universe into
        // temporary registries, cross-validate the profile's DTOs on top of the
        // union, then filter back to the profile's own content.  The vanilla
        // tag universe is retained so the profile's `#tag` supported_items
        // references stay interpretable downstream.
        auto own = RegistryLoader::resolve_own_content(ench_data, eq_data);

        // Compute limited_level uniformly (B-T18): the profile's own registry,
        // using the attached vanilla-universe resolver, BEFORE the profile is
        // constructed (Profile exposes only const registry access).
        auto resolver = besq::data::make_builtin_tag_resolver();
        LimitedLevelCalculator::compute(own.ench, *resolver, load_item_properties());

        // Construct Profile via full-parameter constructor.
        std::string stem = path.stem().string();
        profile = Profile(ProfileMetadata(stem), std::move(own.ench),
                          std::move(own.eq), std::move(own.tags));
        // Restore the declared dependencies parsed in Phase 1b.
        profile.set_dependencies(std::move(dependencies));
        // Attach the vanilla tag universe resolver so the profile's `#tag`
        // supported_items references resolve at the business→algorithm boundary
        // (T7/T10: real MC item tags are the applicability source of truth).
        profile.set_tag_resolver(std::move(resolver));

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
        // Compute limited_level uniformly (B-T18) with the builtin resolver.
        auto resolver = besq::data::make_builtin_tag_resolver();
        LimitedLevelCalculator::compute(ench_reg, *resolver, load_item_properties());
        profile = Profile(ProfileMetadata("builtin:vanilla"), std::move(ench_reg),
                          std::move(eq_reg), std::move(tag_reg));
        profile.set_tag_resolver(std::move(resolver));
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
