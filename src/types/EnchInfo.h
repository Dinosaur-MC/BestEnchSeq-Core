#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "types/ForgeConfig.h"

struct EnchInfo {
    std::string name_id;
    std::string name;
    MCE supported_platform;
    int32_t max_level;
    int32_t limited_level;
    int32_t multiplier;
    bool is_treasure         = false;
    std::unordered_set<std::string> exclusive_set;
    std::unordered_set<int32_t> applicable_category_ids;

    bool operator==(const EnchInfo& other) const;

    struct Hash {
        size_t operator()(const EnchInfo& info) const { return std::hash<std::string>()(info.name_id); }
    };
};

using EnchInfoList = std::vector<EnchInfo>;
