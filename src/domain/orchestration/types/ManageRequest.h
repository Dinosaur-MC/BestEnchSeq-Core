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
    std::string profile_name;   // profile identity key (B-T13: plain string)
    std::string source_name;
    std::string dest_name;
    EnchInfo ench_info;
    Equipment equip;
    std::string category_name;
};
