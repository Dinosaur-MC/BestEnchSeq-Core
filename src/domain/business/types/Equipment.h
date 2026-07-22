#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// ─── Equipment category ───
//
// Numeric ID + string name_id. IDs are assigned by EquipmentCategoryRegistry.
// ID 0 = "any" (matches all equipment categories).
struct EquipmentCategory {
    int32_t id;
    std::string name_id;

    bool operator==(const EquipmentCategory &other) const { return id == other.id; }
    bool operator!=(const EquipmentCategory &other) const { return id != other.id; }

    // ── Builtin category IDs (stable across versions) ──
    static constexpr int32_t ID_ANY         = 0;
    static constexpr int32_t ID_SWORD       = 1;
    static constexpr int32_t ID_HELMET      = 2;
    static constexpr int32_t ID_CHESTPLATE  = 3;
    static constexpr int32_t ID_LEGGINGS    = 4;
    static constexpr int32_t ID_BOOTS       = 5;
    static constexpr int32_t ID_PICKAXE     = 6;
    static constexpr int32_t ID_AXE         = 7;
    static constexpr int32_t ID_SHOVEL      = 8;
    static constexpr int32_t ID_HOE         = 9;
    static constexpr int32_t ID_BOW         = 10;
    static constexpr int32_t ID_SHIELD      = 11;
    static constexpr int32_t ID_CROSSBOW    = 12;
    static constexpr int32_t ID_TRIDENT     = 13;
    static constexpr int32_t ID_FISHING_ROD = 14;

    // ── Builtin name IDs (no namespace prefix) ──
    static constexpr std::string_view NAME_ANY         = "any";
    static constexpr std::string_view NAME_SWORD       = "sword";
    static constexpr std::string_view NAME_HELMET      = "helmet";
    static constexpr std::string_view NAME_CHESTPLATE  = "chestplate";
    static constexpr std::string_view NAME_LEGGINGS    = "leggings";
    static constexpr std::string_view NAME_BOOTS       = "boots";
    static constexpr std::string_view NAME_PICKAXE     = "pickaxe";
    static constexpr std::string_view NAME_AXE         = "axe";
    static constexpr std::string_view NAME_SHOVEL      = "shovel";
    static constexpr std::string_view NAME_HOE         = "hoe";
    static constexpr std::string_view NAME_BOW         = "bow";
    static constexpr std::string_view NAME_SHIELD      = "shield";
    static constexpr std::string_view NAME_CROSSBOW    = "crossbow";
    static constexpr std::string_view NAME_TRIDENT     = "trident";
    static constexpr std::string_view NAME_FISHING_ROD = "fishing_rod";
};

template <> struct std::hash<EquipmentCategory> {
    size_t operator()(const EquipmentCategory &cat) const noexcept { return std::hash<int32_t>()(cat.id); }
};

// ─── Equipment ───
//
// Represents a specific piece of equipment (e.g. "minecraft:diamond_sword").
// Each equipment has a category_id referencing EquipmentCategoryRegistry.
// Equipment instances are managed by EquipmentRegistry (vector index = id).
struct Equipment {
    std::string name_id;
    std::string name;
    int32_t category_id;
    int32_t max_durability;

    bool operator==(const Equipment &other) const;
};

template <> struct std::hash<Equipment> {
    size_t operator()(const Equipment &eq) const noexcept { return std::hash<std::string>()(eq.name_id); }
};