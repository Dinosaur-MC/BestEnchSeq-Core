#pragma once
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "common/CommonTypes.h"
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
    NSID profile_name;
    NSID source_name;
    NSID dest_name;
    EnchInfo ench_info;
    Equipment equip;
    std::string category_name;
};
