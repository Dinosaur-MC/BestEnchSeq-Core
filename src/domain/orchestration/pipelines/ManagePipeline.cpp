#include "ManagePipeline.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "domain/business/types/Profile.h"

#include <string>

ManageResult ManagePipeline::run(
    ProfileManager& profiles,
    ProfileLoader& loader,
    const ManageRequest& request)
{
    ManageResult result;

    switch (request.action) {

    case ManageRequest::Action::LoadBuiltin: {
        auto& profile = profiles.create("builtin:vanilla");
        loader.load_builtin(profile);
        profiles.activate("builtin:vanilla");
        result.message = "Loaded built-in vanilla data";
        break;
    }

    case ManageRequest::Action::LoadFile: {
        auto& profile = profiles.active();
        auto [ench_data, eq_data, item_tags] = FormatDetector::parse(request.file_path);
        TagRegistry tag_reg;
        EquipmentRegistry eq_reg;
        EnchantmentRegistry ench_reg;
        RegistryLoader reg_loader;
        // Seed tag resolution with the active profile's tags (vanilla fallback)
        // PLUS a datapack's own item tags so `#mypack:*` supported_items
        // references survive cross-validation (B-T24 #24).
        TagRegistry base_tags;
        for (const auto& [nsid, tag] : profile.tags().data())
            base_tags.insert(tag);
        auto datapack_tags = McOfficialParser::build_item_tag_registry(item_tags);
        for (const auto& [nsid, tag] : datapack_tags.data())
            base_tags.insert(tag);
        reg_loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg,
                           &base_tags);
        if (auto resolver = profile.tag_resolver_ptr())
            McOfficialParser::load_item_tags_into(*resolver, item_tags);
        for (const auto& [nsid, tag] : tag_reg.data())
            profile.add_tag(tag);
        for (const auto& [nsid, eq] : eq_reg.data())
            profile.add_equipment(eq);
        for (const auto& [nsid, info] : ench_reg.data())
            profile.add_enchantment(info);
        result.message = "Loaded: " + request.file_path;
        break;
    }

    case ManageRequest::Action::LoadData: {
        for (const auto& filter : request.filters) {
            if (!filter.empty()) {
                ManageRequest sub;
                sub.action = ManageRequest::Action::LoadFile;
                sub.file_path = filter;
                auto sub_result = run(profiles, loader, sub);
                if (!sub_result.success)
                    return sub_result;
            }
        }
        break;
    }

    case ManageRequest::Action::CreateProfile: {
        profiles.create(request.profile_name);
        result.message = "Created profile: " + request.profile_name;
        break;
    }

    case ManageRequest::Action::ActivateProfile: {
        profiles.activate(request.profile_name);
        result.message = "Activated profile: " + request.profile_name;
        break;
    }

    case ManageRequest::Action::ForkProfile: {
        profiles.branch(request.source_name, request.dest_name);
        result.message = "Forked " + request.source_name
                       + " -> " + request.dest_name;
        break;
    }

    case ManageRequest::Action::MergeProfile: {
        profiles.merge(request.source_name, request.dest_name);
        result.message = "Merged " + request.source_name
                       + " -> " + request.dest_name;
        break;
    }

    case ManageRequest::Action::RemoveProfile: {
        profiles.remove(request.profile_name);
        result.message = "Removed profile: " + request.profile_name;
        break;
    }

    case ManageRequest::Action::ListProfiles: {
        auto names = profiles.list();
        result.profile_list.reserve(names.size());
        for (const auto& name : names)
            result.profile_list.push_back(name);
        result.message = std::to_string(result.profile_list.size()) + " profiles";
        break;
    }

    case ManageRequest::Action::AddEnchantment: {
        result.success = profiles.add_enchantment(profiles.active_name(), request.ench_info);
        break;
    }

    case ManageRequest::Action::RemoveEnchantment: {
        result.success = profiles.remove_enchantment(profiles.active_name(), NSID(request.profile_name));
        break;
    }

    case ManageRequest::Action::ModifyEnchantment: {
        auto& active = profiles.active();
        try {
            auto current = active.ench().at(NSID(request.profile_name));
            if (request.ench_info.multiplier > 0)
                current.multiplier = request.ench_info.multiplier;
            if (request.ench_info.max_level > 0)
                current.max_level = request.ench_info.max_level;
            if (request.ench_info.limited_level >= 0)
                current.limited_level = request.ench_info.limited_level;
            result.success = profiles.update_enchantment(profiles.active_name(), current);
        } catch (const std::out_of_range&) {
            result.success = false;
            result.message = "Enchantment not found";
        }
        break;
    }

    case ManageRequest::Action::AddEquipment: {
        result.success = profiles.add_equipment(profiles.active_name(), request.equip);
        break;
    }

    case ManageRequest::Action::RemoveEquipment: {
        result.success = profiles.remove_equipment(profiles.active_name(), NSID(request.profile_name));
        break;
    }

    case ManageRequest::Action::AddCategory: {
        NSID cat_nsid("#minecraft:" + request.category_name);
        if (profiles.active().tags().contains(cat_nsid)) {
            result.success = false;
            result.message = "Category already exists";
        } else {
            result.success = profiles.add_tag(profiles.active_name(), {cat_nsid, request.category_name});
        }
        break;
    }
    }

    return result;
}
