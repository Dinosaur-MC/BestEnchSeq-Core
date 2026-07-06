#pragma once
#include "types/common.h"
#include "types/EquipmentType.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EquipmentRegistry {
public:
    static EquipmentRegistry& get_instance() {
        static EquipmentRegistry instance;
        return instance;
    }

    EquipmentRegistry() = default;
    EquipmentRegistry(const EquipmentRegistry&) = delete;
    EquipmentRegistry& operator=(const EquipmentRegistry&) = delete;

    void initialize(const std::vector<EquipmentType>& eq_list);

    const EquipmentType* get(const std::string& id) const;
    const std::vector<EquipmentType>& get_instances() const { return equipment_list_; }

    // Custom equipment categories (moved from EquipmentCategory statics)
    void register_custom_equipment(const EquipmentCategory& category);
    void reset_custom_equipment();
    bool is_custom_equipment(const EquipmentCategory& category) const;
    const std::unordered_set<EquipmentCategory>& get_custom_equipments() const { return custom_equipments_; }

private:
    std::vector<EquipmentType> equipment_list_;
    std::unordered_map<std::string, const EquipmentType*> equipment_map_;
    std::unordered_set<EquipmentCategory> custom_equipments_;
};
