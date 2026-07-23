#include "EquipmentRegistry.h"
#include <stdexcept>

void EquipmentRegistry::reset_for_testing() {
    instances_.clear();
    name_to_id_.clear();
}

void EquipmentRegistry::initialize(const std::vector<Equipment>& eq_list) {
    reset_for_testing();

    for (const auto& eq : eq_list) {
        instances_.push_back(eq);
        name_to_id_[eq.id] = static_cast<int32_t>(instances_.size()) - 1;
    }
}

const Equipment& EquipmentRegistry::get(int32_t id) const {
    if (id >= 0 && static_cast<size_t>(id) < instances_.size())
        return instances_[static_cast<size_t>(id)];
    throw std::out_of_range("Unknown Equipment id: " + std::to_string(id));
}

const Equipment& EquipmentRegistry::get(const NSID& id) const {
    auto it = name_to_id_.find(id);
    if (it != name_to_id_.end())
        return instances_[static_cast<size_t>(it->second)];
    throw std::out_of_range("Unknown Equipment: " + id.str());
}

int32_t EquipmentRegistry::get_id(const NSID& id) const {
    auto it = name_to_id_.find(id);
    if (it != name_to_id_.end()) return it->second;
    // Fallback: bare NSID → try with "minecraft:" prefix
    if (id.get_ns().empty()) {
        NSID prefixed("minecraft:" + id.get_id());
        auto ns_it = name_to_id_.find(prefixed);
        if (ns_it != name_to_id_.end()) return ns_it->second;
    }
    return -1;
}

std::vector<const Equipment*> EquipmentRegistry::get_by_category(const NSID& category) const {
    std::vector<const Equipment*> result;
    for (const auto& eq : instances_) {
        if (eq.category == category) {
            result.push_back(&eq);
        }
    }
    return result;
}

std::unordered_map<NSID, const Equipment*> EquipmentRegistry::get_name_map() const {
    std::unordered_map<NSID, const Equipment*> result;
    result.reserve(instances_.size());
    for (const auto& eq : instances_) {
        result[eq.id] = &eq;
    }
    return result;
}

bool EquipmentRegistry::add(const Equipment& eq) {
    if (name_to_id_.count(eq.id)) return false;
    int32_t idx = static_cast<int32_t>(instances_.size());
    instances_.push_back(eq);
    name_to_id_[eq.id] = idx;
    return true;
}

bool EquipmentRegistry::remove(const NSID& id) {
    auto it = name_to_id_.find(id);
    if (it == name_to_id_.end()) return false;
    name_to_id_.erase(it);
    // Keep index stability for existing references; mark as invalid
    instances_[it->second] = Equipment{};
    return true;
}
