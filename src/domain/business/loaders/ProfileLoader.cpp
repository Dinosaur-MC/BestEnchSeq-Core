#include "ProfileLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/components/LimitedLevelCalculator.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/McOfficialParser.h"
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
        // loaded separately below as the cross-validation fallback.  For a
        // datapack dir the result also carries the datapack's own item-tag
        // definitions (data/<ns>/tags/item/*.json) so `#mypack:*`
        // supported_items references survive cross-validation (B-T24 #24).
        auto [ench_data, eq_data, item_tags] = FormatDetector::parse(path);

        // Phase 1b: parse the profile's declared dependencies AND the profile
        // KEY from the raw JSON root.  FormatDetector::Result carries
        // enchantments/equipment/item_tags but NOT the top-level `dependencies`
        // array or `name`, so re-read them here.  (CSV / MC-official formats
        // have no JSON name/dependencies — the profile key falls back to the
        // file stem.)
        std::vector<std::string> dependencies;
        std::string json_name;
        const auto format = FormatDetector::detect(path);
        if (format == DataFormat::NativeJson || format == DataFormat::Unknown) {
            auto root = Json::parse(file_utils::read_file(path));
            // B-T26 #18: JSON top-level `name` is the profile KEY when present
            // and non-empty (user-confirmed "name 优先，fallback 文件/目录名").
            if (root.has(std::string(ProfileMetadata::KEY_NAME)))
                json_name = root[std::string(ProfileMetadata::KEY_NAME)].as<std::string>();
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
        //
        // The datapack's own item tags must seed the validation universe so
        // `#mypack:*` supported_items references resolve during
        // cross-validation, and must land in the profile's tag registry so the
        // profile owns them (mirrors ProfileManager::load_datapack, B-T14 I-1).
        // FormatDetector::Result carries item_tags for the McOfficial branch;
        // native JSON/CSV have none, so this is a no-op for them (B-T24 #24).
        auto datapack_tags = McOfficialParser::build_item_tag_registry(item_tags);
        auto own = RegistryLoader::resolve_own_content(
            ench_data, eq_data, item_tags.empty() ? nullptr : &datapack_tags);

        // Compute limited_level uniformly (B-T18): the profile's own registry,
        // using the attached vanilla-universe resolver, BEFORE the profile is
        // constructed (Profile exposes only const registry access).  Datapack
        // item tags are loaded into the resolver (honoring `replace`) so
        // `#mypack:*` / `tags_of` applicability holds at solve time.
        auto resolver = besq::data::make_builtin_tag_resolver();
        McOfficialParser::load_item_tags_into(*resolver, item_tags);
        LimitedLevelCalculator::compute(own.ench, *resolver, load_item_properties());

        // Construct Profile via full-parameter constructor.  The profile KEY is
        // the JSON top-level `name` when present and non-empty (B-T26 #18), so
        // load_directory / load / load_into agree on the key for the same file
        // regardless of the path.  Otherwise fall back to the file stem.
        // Datapack dirs (McOfficial) are unaffected — they keep the folder-stem
        // key.
        std::string key = path.stem().string();
        if (!json_name.empty())
            key = std::move(json_name);
        profile = Profile(ProfileMetadata(std::move(key)), std::move(own.ench),
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
