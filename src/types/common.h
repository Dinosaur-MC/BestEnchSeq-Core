#pragma once
#include <cstdint>
#include <string>

namespace platform {

enum MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

}; // namespace platform

struct EquipmentCategory : public std::string {
    using std::string::string;

    static constexpr const EquipmentCategory Any() { return EquipmentCategory("any"); }
    static constexpr const EquipmentCategory Helmet() { return EquipmentCategory("helmet"); }
    static constexpr const EquipmentCategory Chestplate() { return EquipmentCategory("chestplate"); }
    static constexpr const EquipmentCategory Leggings() { return EquipmentCategory("leggings"); }
    static constexpr const EquipmentCategory Boots() { return EquipmentCategory("boots"); }
    static constexpr const EquipmentCategory Sword() { return EquipmentCategory("sword"); }
    static constexpr const EquipmentCategory Pickaxe() { return EquipmentCategory("pickaxe"); }
    static constexpr const EquipmentCategory Axe() { return EquipmentCategory("axe"); }
    static constexpr const EquipmentCategory Shovel() { return EquipmentCategory("shovel"); }
    static constexpr const EquipmentCategory Hoe() { return EquipmentCategory("hoe"); }
    static constexpr const EquipmentCategory Bow() { return EquipmentCategory("bow"); }
    static constexpr const EquipmentCategory Shield() { return EquipmentCategory("shield"); }
    static constexpr const EquipmentCategory Crossbow() { return EquipmentCategory("crossbow"); }
    static constexpr const EquipmentCategory Trident() { return EquipmentCategory("trident"); }
    static constexpr const EquipmentCategory FishingRod() { return EquipmentCategory("fishing_rod"); }
};
