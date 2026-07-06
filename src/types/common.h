#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

namespace platform {

enum MCE : int8_t {
    None = 0x00,
    Java = 0x01,
    Bedrock = 0x02,
    All = 0x03,
};

} // namespace platform

struct EquipmentCategory : public std::string {
    using std::string::string;

    static constexpr EquipmentCategory Any() { return "any"; }
    static constexpr EquipmentCategory Helmet() { return "helmet"; }
    static constexpr EquipmentCategory Chestplate() { return "chestplate"; }
    static constexpr EquipmentCategory Leggings() { return "leggings"; }
    static constexpr EquipmentCategory Boots() { return "boots"; }
    static constexpr EquipmentCategory Sword() { return "sword"; }
    static constexpr EquipmentCategory Pickaxe() { return "pickaxe"; }
    static constexpr EquipmentCategory Axe() { return "axe"; }
    static constexpr EquipmentCategory Shovel() { return "shovel"; }
    static constexpr EquipmentCategory Hoe() { return "hoe"; }
    static constexpr EquipmentCategory Bow() { return "bow"; }
    static constexpr EquipmentCategory Shield() { return "shield"; }
    static constexpr EquipmentCategory Crossbow() { return "crossbow"; }
    static constexpr EquipmentCategory Trident() { return "trident"; }
    static constexpr EquipmentCategory FishingRod() { return "fishing_rod"; }

    EquipmentCategory() = default;
    // No _custom_equipments statics — moved to EquipmentRegistry
};

namespace std {
template <> struct hash<EquipmentCategory> {
    size_t operator()(const EquipmentCategory& cat) const { return hash<string>()(cat); }
};
} // namespace std
