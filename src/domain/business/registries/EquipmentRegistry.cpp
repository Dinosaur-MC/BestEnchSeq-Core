#include "EquipmentRegistry.h"

EquipmentRegistry::EquipmentRegistry(const std::vector<Equipment>& eq_list) {
    _data.reserve(eq_list.size());
    for (const auto& eq : eq_list)
        _data.emplace(eq.id, eq);
}

std::vector<Equipment> EquipmentRegistry::get_by_category(const NSID& category) const {
    std::vector<Equipment> result;
    for (const auto& [id, eq] : _data)
        if (eq.category == category)
            result.push_back(eq);
    return result;
}
