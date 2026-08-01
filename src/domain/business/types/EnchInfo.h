#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IJsonSerializable.h"
#include <string>
#include <unordered_set>
#include <vector>

struct EnchInfo : IJsonSerializable {
    NSID id;
    std::string name;
    MCE supported_platform = MCE::None;
    int32_t max_level      = 0;
    int32_t limited_level  = 0;
    int32_t multiplier     = 0;
    bool is_treasure       = false;
    std::unordered_set<NSID> exclusive_set;
    std::unordered_set<NSID> supported_items;   // MC 原生：`#tag` 引用或具体物品 NSID

    EnchInfo() = default;
    EnchInfo(NSID id_, std::string name_, MCE platform_, int32_t max_level_,
             int32_t limited_level_, int32_t multiplier_, bool is_treasure_,
             std::unordered_set<NSID> exclusive_set_,
             std::unordered_set<NSID> supported_items_)
        : id(std::move(id_)), name(std::move(name_)), supported_platform(platform_),
          max_level(max_level_), limited_level(limited_level_), multiplier(multiplier_),
          is_treasure(is_treasure_), exclusive_set(std::move(exclusive_set_)),
          supported_items(std::move(supported_items_)) {}

    bool operator==(const EnchInfo &o) const { return id == o.id; }
    auto operator<=>(const EnchInfo &o) const { return id <=> o.id; }

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

template <> struct std::hash<EnchInfo> {
    size_t operator()(const EnchInfo &info) const { return std::hash<NSID>()(info.id); }
};

using EnchInfoList = std::vector<EnchInfo>;
