#include "ManagePipeline.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/components/FormatDetector.h"
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
        auto& profile = profiles.create(NSID("builtin:vanilla"));
        loader.load_builtin(profile);
        profiles.activate(NSID("builtin:vanilla"));
        result.message = "Loaded built-in vanilla data";
        break;
    }

    case ManageRequest::Action::LoadFile: {
        auto& profile = profiles.active();
        auto [ench_data, eq_data] = FormatDetector::parse(request.file_path);
        TagRegistry tag_reg;
        EquipmentRegistry eq_reg;
        EnchantmentRegistry ench_reg;
        RegistryLoader reg_loader;
        // Seed tag resolution with the active profile's tags (vanilla fallback).
        reg_loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg,
                           &profile.tags());
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
        result.message = "Created profile: " + request.profile_name.str();
        break;
    }

    case ManageRequest::Action::ActivateProfile: {
        profiles.activate(request.profile_name);
        result.message = "Activated profile: " + request.profile_name.str();
        break;
    }

    case ManageRequest::Action::ForkProfile: {
        profiles.branch(request.source_name, request.dest_name);
        result.message = "Forked " + request.source_name.str()
                       + " -> " + request.dest_name.str();
        break;
    }

    case ManageRequest::Action::MergeProfile: {
        profiles.merge(request.source_name, request.dest_name);
        result.message = "Merged " + request.source_name.str()
                       + " -> " + request.dest_name.str();
        break;
    }

    case ManageRequest::Action::RemoveProfile: {
        profiles.remove(request.profile_name);
        result.message = "Removed profile: " + request.profile_name.str();
        break;
    }

    case ManageRequest::Action::ListProfiles: {
        auto nsids = profiles.list();
        result.profile_list.reserve(nsids.size());
        for (const auto& nsid : nsids)
            result.profile_list.push_back(nsid.str());
        result.message = std::to_string(result.profile_list.size()) + " profiles";
        break;
    }

    case ManageRequest::Action::AddEnchantment: {
        result.success = profiles.active().add_enchantment(request.ench_info);
        break;
    }

    case ManageRequest::Action::RemoveEnchantment: {
        result.success = profiles.active().remove_enchantment(request.profile_name);
        break;
    }

    case ManageRequest::Action::ModifyEnchantment: {
        auto& active = profiles.active();
        try {
            auto current = active.ench().at(request.profile_name);
            if (request.ench_info.multiplier > 0)
                current.multiplier = request.ench_info.multiplier;
            if (request.ench_info.max_level > 0)
                current.max_level = request.ench_info.max_level;
            if (request.ench_info.limited_level >= 0)
                current.limited_level = request.ench_info.limited_level;
            result.success = active.update_enchantment(current);
        } catch (const std::out_of_range&) {
            result.success = false;
            result.message = "Enchantment not found";
        }
        break;
    }

    case ManageRequest::Action::AddEquipment: {
        result.success = profiles.active().add_equipment(request.equip);
        break;
    }

    case ManageRequest::Action::RemoveEquipment: {
        result.success = profiles.active().remove_equipment(request.profile_name);
        break;
    }

    case ManageRequest::Action::AddCategory: {
        NSID cat_nsid("#minecraft:" + request.category_name);
        if (profiles.active().tags().contains(cat_nsid)) {
            result.success = false;
            result.message = "Category already exists";
        } else {
            result.success = profiles.active().add_tag({cat_nsid, request.category_name});
        }
        break;
    }
    }

    return result;
}
