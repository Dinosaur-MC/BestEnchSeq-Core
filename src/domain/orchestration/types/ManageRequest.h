#pragma once
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include <string>
#include <vector>

struct ManageRequest {
    enum class Action {
        LoadBuiltin,
        LoadFile,
        LoadData,
        CreateProfile,
        ActivateProfile,
        ForkProfile,
        MergeProfile,
        RemoveProfile,
        ListProfiles,
        AddEnchantment,
        RemoveEnchantment,
        ModifyEnchantment,
        AddEquipment,
        RemoveEquipment,
        AddCategory,
    };

    Action action;
    std::string file_path;
    std::vector<std::string> filters;
    // DUAL-USE: a profile identity key (plain string, B-T13) for
    // CreateProfile / ActivateProfile / RemoveProfile, but repurposed as an
    // enchantment/equipment content NSID for RemoveEnchantment /
    // ModifyEnchantment / RemoveEquipment.  Callers must interpret it by the
    // action; ManagePipeline wraps it in NSID(...) where it denotes content.
    std::string profile_name;
    std::string source_name;
    std::string dest_name;
    EnchInfo ench_info;
    Equipment equip;
    std::string category_name;
};
