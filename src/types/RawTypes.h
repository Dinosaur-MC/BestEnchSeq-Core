#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

// ─── Raw (pre-resolution) intermediate types ──────────────────────────────
//
// These types are produced by parser layer functions and consumed by
// RegistryResolver to convert string-based references into resolved int32_t
// IDs.  They decouple parsing from registry availability: parsers never
// call into the registry layer.

/// A namespaced identifier (e.g. "minecraft:sharpness").
struct Id {
    std::string ns = "minecraft";
    std::string path;

    [[nodiscard]] std::string str() const noexcept { return ns + ":" + path; }

    bool operator==(const Id& o) const noexcept { return ns == o.ns && path == o.path; }
    bool operator<(const Id& o) const noexcept {
        if (ns != o.ns) return ns < o.ns;
        return path < o.path;
    }
};

/// String-based intermediate representation of an enchantment definition,
/// produced by parsers before registry resolution.
///
/// All category references are still strings — resolution to integer IDs
/// happens in a separate RegistryResolver step.
struct RawEnchantment {
    Id id;
    std::string display_name;
    int32_t multiplier = 0;
    int32_t max_level = 0;
    int32_t limited_level = 0;       // 0 = treasure (enchanting table unreachable)

    std::unordered_set<std::string> exclusive_set;       // enchantment ID strings (awaiting resolution)
    std::unordered_set<std::string> applicable_items;    // item/category name strings (awaiting resolution)

};

/// String-based intermediate representation of an equipment definition.
struct RawEquipment {
    Id id;
    std::string display_name;
    std::string category;            // category name string, not yet resolved to int32_t
    int32_t max_durability = 0;
};
