#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

/// Per-item properties for MC official data pack processing.
/// These are game-design constants that Minecraft's data pack format
/// does not include, but are needed for equipment derivation and
/// limited_level calculation.
struct ItemProperty {
    int32_t durability = 0;       // max_durability (0 = unknown)
    int32_t enchantability = -1;  // enchanting power (-1 = unknown)
    std::string category;         // equipment category name
};

/// Load item properties from the embedded item_properties.json data.
/// Falls back to filesystem path if embedded data is unavailable.
/// Returns a map of item short ID → property.
std::unordered_map<std::string, ItemProperty> load_item_properties();
