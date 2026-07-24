#pragma once
#include <cstdint>
#include <string>

namespace business::loader {

/// Data transfer object for equipment data produced by parsers.
struct EquipmentData {
    std::string id;               ///< "minecraft:diamond_sword"
    std::string display_name;
    std::string category;         ///< "sword"
    int32_t max_durability = 0;
};

} // namespace business::loader
