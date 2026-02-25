#include "common.h"

std::unordered_set<EquipmentCategory> EquipmentCategory::_custom_equipments = {};

void EquipmentCategory::register_custom_equipment(const EquipmentCategory &category) {
    _custom_equipments.insert(category);
}
void EquipmentCategory::reset_custom_equipment() { _custom_equipments.clear(); }
const std::unordered_set<EquipmentCategory> &EquipmentCategory::get_custom_equipments() {
    return _custom_equipments;
}
