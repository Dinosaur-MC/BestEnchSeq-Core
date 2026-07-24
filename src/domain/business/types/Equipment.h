#pragma once
#include "common/CommonTypes.h"
#include <string>

// ─── Equipment ───
//
// Represents a specific piece of equipment (e.g. "minecraft:diamond_sword").
// Each equipment has a category NSID referencing an EquipmentCategory / EquipmentTag.
// Equipment instances are managed by EquipmentRegistry (vector index = id).
struct Equipment {
    NSID id;
    std::string name;
    NSID category;
    int32_t max_durability;

    bool operator==(const Equipment &o) const { return id == o.id; }
    bool operator<(const Equipment &o) const { return id.str() < o.id.str(); }
};

template <> struct std::hash<Equipment> {
    size_t operator()(const Equipment &eq) const noexcept { return std::hash<NSID>()(eq.id); }
};
