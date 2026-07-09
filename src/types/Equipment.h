#pragma once
#include "EnchInfo.h"
#include "EnchSet.h"

// ─── Equipment type ───
//
// Represents a specific piece of equipment (e.g. "minecraft:diamond_sword").
// Each equipment has a category_id referencing EquipmentCategoryRegistry.
// Equipment instances are managed by EquipmentRegistry (vector index = id).
struct Equipment {
    const std::string name_id;
    const std::string name;
    const int32_t category_id;
    const int32_t max_durability;

    struct Hash {
        size_t operator()(const Equipment& eq) const { return std::hash<std::string>()(eq.name_id); }
    };

    bool operator==(const Equipment& other) const;

    bool is_applicable(const std::string& ench) const;
    bool is_applicable(const Ench& ench) const;

    EnchSet filter_enchantments(const EnchSet& enchantments) const;
    EnchInfoList filter_enchantments(const EnchInfoList& enchantments) const;

    // Durability helpers
    static int32_t merge_durability(int32_t d1, int32_t d2, int32_t max_d);
    static int32_t repair_durability(int32_t d, int32_t n, int32_t max_d);
    int32_t calc_merge_durability(int32_t d1, int32_t d2) const;
    int32_t calc_repair_durability(int32_t d, int32_t n) const;
};
