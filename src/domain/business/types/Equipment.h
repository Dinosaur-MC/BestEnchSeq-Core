#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IJsonSerializable.h"
#include <string>

// ─── Equipment ───
//
// Represents a specific piece of equipment (e.g. "minecraft:diamond_sword").
// Each equipment has a category NSID referencing an EquipmentCategory / EquipmentTag.
// Equipment instances are managed by EquipmentRegistry (vector index = id).
struct Equipment : IJsonSerializable {
    NSID id;
    std::string name;
    NSID category;
    int32_t max_durability;

    Equipment() = default;
    Equipment(NSID id_, std::string name_, NSID category_, int32_t max_durability_)
        : id(std::move(id_)), name(std::move(name_)), category(std::move(category_)),
          max_durability(max_durability_) {}

    bool operator==(const Equipment &o) const { return id == o.id; }
    auto operator<=>(const Equipment &o) const { return id <=> o.id; }

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

template <> struct std::hash<Equipment> {
    size_t operator()(const Equipment &eq) const noexcept { return std::hash<NSID>()(eq.id); }
};
