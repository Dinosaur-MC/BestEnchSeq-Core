#pragma once
#include "common/CommonTypes.h"
#include <string>
#include <unordered_set>
#include <vector>

struct EnchInfo {
    NSID id;
    std::string name;
    MCE supported_platform = MCE::None;
    int32_t max_level      = 0;
    int32_t limited_level  = 0;
    int32_t multiplier     = 0;
    bool is_treasure       = false;
    std::unordered_set<NSID> exclusive_set;
    std::unordered_set<NSID> applicable_equipments;

    bool operator==(const EnchInfo &o) const { return id == o.id; }
    bool operator<(const EnchInfo &o) const { return id.str() < o.id.str(); }

    struct Hash {
        size_t operator()(const EnchInfo &info) const { return std::hash<NSID>()(info.id); }
    };
};

using EnchInfoList = std::vector<EnchInfo>;
