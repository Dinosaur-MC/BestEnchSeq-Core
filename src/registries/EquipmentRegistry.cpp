#include "registries/EquipmentRegistry.h"

EquipmentRegistry& EquipmentRegistry::get_instance() {
    static EquipmentRegistry instance;
    return instance;
}

void EquipmentRegistry::initialize(const std::vector<Equipment>& eq_list) {
    instances_.clear();
    name_to_id_.clear();

    for (const auto& eq : eq_list) {
        instances_.push_back(eq);
        name_to_id_[eq.name_id] = static_cast<int32_t>(instances_.size()) - 1;
    }
}

const Equipment* EquipmentRegistry::get(int32_t id) const {
    if (id >= 0 && id < static_cast<int32_t>(instances_.size()))
        return &instances_[static_cast<size_t>(id)];
    return nullptr;
}

const Equipment* EquipmentRegistry::get(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return &instances_[static_cast<size_t>(it->second)];
    return nullptr;
}

int32_t EquipmentRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    return it != name_to_id_.end() ? it->second : -1;
}
