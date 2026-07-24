#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "common/CommonTypes.h"

// ─── Raw (pre-resolution) intermediate types ──────────────────────────────
//
// These types are produced by parser layer functions and consumed by
// RawTypeAdapter to convert string-based references into resolved NSID IDs.
// They decouple parsing from registry availability: parsers never
// call into the registry layer.

/// String-based intermediate representation of an enchantment definition,
/// produced by parsers before registry resolution.
///
/// All category references are still strings — resolution to NSIDs
/// happens in a separate RawTypeAdapter step.
struct RawEnchantment {
    NSID id;
    std::string display_name;
    int32_t multiplier = 0;
    int32_t max_level = 0;
    int32_t limited_level = 0;       // 0 = treasure (enchanting table unreachable)

    std::unordered_set<std::string> exclusive_set;       // enchantment ID strings (awaiting resolution)
    std::unordered_set<std::string> applicable_items;    // item/category name strings (awaiting resolution)

};

/// String-based intermediate representation of an equipment definition.
struct RawEquipment {
    NSID id;
    std::string display_name;
    std::string category;            // category name string, not yet resolved to NSID
    int32_t max_durability = 0;
};
