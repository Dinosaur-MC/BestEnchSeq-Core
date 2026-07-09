#pragma once
#include <cstdint>
#include <string>

// ─── Equipment category ───
//
// Numeric ID + string name_id. IDs are assigned by EquipmentCategoryRegistry.
// ID 0 = "any" (matches all equipment categories).
struct EquipmentCategory {
    int32_t id;
    std::string name_id;

    bool operator==(const EquipmentCategory& other) const { return id == other.id; }
    bool operator!=(const EquipmentCategory& other) const { return id != other.id; }
};

namespace std {
template <> struct hash<EquipmentCategory> {
    size_t operator()(const EquipmentCategory& cat) const noexcept {
        return std::hash<int32_t>()(cat.id);
    }
};
}
