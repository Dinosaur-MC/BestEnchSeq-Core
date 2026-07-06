#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "common.h"

struct EnchInfo {
    const std::string name_id;
    const std::string name;
    const platform::MCE supported_platform;
    const int32_t max_level;
    const int32_t limited_level;
    const int32_t multiplier;
    const std::unordered_set<std::string> exclusive_set;
    const std::unordered_set<EquipmentCategory> applicable_equipment;

    bool operator==(const EnchInfo& other) const;

    struct Hash {
        size_t operator()(const EnchInfo& info) const { return std::hash<std::string>()(info.name_id); }
    };
};

using EnchInfoList = std::vector<EnchInfo>;
