#pragma once
#include <cstdint>
#include <string>
//
// Represents a specific piece of equipment (e.g. "minecraft:diamond_sword").
// Each equipment has a category_id referencing EquipmentCategoryRegistry.
// Equipment instances are managed by EquipmentRegistry (vector index = id).
struct Equipment {
    std::string name_id;
    std::string name;
    int32_t category_id;
    int32_t max_durability;

    struct Hash {
        size_t operator()(const Equipment& eq) const { return std::hash<std::string>()(eq.name_id); }
    };

    bool operator==(const Equipment& other) const;

    // NOTE: Registry-dependent queries (is_applicable, filter_enchantments)
    // and forge utility (durability helpers) have been removed from the domain
    // type. Use EnchantmentRegistry / a future DomainForgeUtil instead.
};
