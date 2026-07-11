#pragma once

#include "BESQTypes.h"
#include <string>
#include <unordered_set>
#include <vector>

/// String-based intermediate representation of an enchantment definition,
/// produced by parsers before registry resolution.
///
/// All category references are still strings — resolution to integer IDs
/// happens in a separate RegistryResolver step.
struct RawEnchInfo {
    std::string name_id;
    std::string name;
    MCE supported_platform;
    int32_t max_level        = 0;
    int32_t limited_level    = 0;
    int32_t multiplier       = 0;
    std::unordered_set<std::string> exclusive_set;          // strings (#tag refs resolved)
    std::unordered_set<std::string> applicable_equipment;    // category name strings
};

/// String-based intermediate representation of an equipment definition.
struct RawEquipment {
    std::string name_id;
    std::string name;
    std::string category;   // category name string, not yet resolved to int32_t
    int32_t max_durability  = 0;
};
