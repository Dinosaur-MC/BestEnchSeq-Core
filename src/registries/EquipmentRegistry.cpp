#include "registries/EquipmentRegistry.h"

void EquipmentRegistry::initialize(const std::vector<EquipmentType>& eq_list) {
    equipment_list_.clear();
    equipment_map_.clear();
    equipment_list_.reserve(eq_list.size());
    for (const auto& eq : eq_list) {
        equipment_list_.push_back(eq);
    }
    for (auto& eq : equipment_list_) {
        equipment_map_[eq.id] = &eq;
    }
}

const EquipmentType* EquipmentRegistry::get(const std::string& id) const {
    auto it = equipment_map_.find(id);
    return it != equipment_map_.end() ? it->second : nullptr;
}

void EquipmentRegistry::register_custom_equipment(const EquipmentCategory& category) {
    custom_equipments_.insert(category);
}

void EquipmentRegistry::reset_custom_equipment() {
    custom_equipments_.clear();
}

bool EquipmentRegistry::is_custom_equipment(const EquipmentCategory& category) const {
    return custom_equipments_.find(category) != custom_equipments_.end();
}
