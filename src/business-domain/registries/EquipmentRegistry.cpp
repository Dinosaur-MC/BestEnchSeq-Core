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
        name_to_id_[eq.name_id] = static_cast<int32_t>(instances_.size()) - 1;
    }
}

const Equipment& EquipmentRegistry::get(int32_t id) const {
    if (id >= 0 && static_cast<size_t>(id) < instances_.size())
        return instances_[static_cast<size_t>(id)];
    throw std::out_of_range("Unknown Equipment id: " + std::to_string(id));
}

const Equipment& EquipmentRegistry::get(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return instances_[static_cast<size_t>(it->second)];
    throw std::out_of_range("Unknown Equipment: " + name_id);
}

int32_t EquipmentRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end()) return it->second;
    // Fallback: bare name → try "minecraft:" prefix
    // (entries are stored as "minecraft:diamond_boots", lookups use "diamond_boots")
    if (name_id.find(':') == std::string::npos) {
        auto ns_it = name_to_id_.find("minecraft:" + name_id);
        if (ns_it != name_to_id_.end()) return ns_it->second;
    }
    return -1;
}

std::vector<const Equipment*> EquipmentRegistry::get_by_category(int32_t category_id) const {
    std::vector<const Equipment*> result;
    for (const auto& eq : instances_) {
        if (eq.category_id == category_id) {
            result.push_back(&eq);
        }
    }
    return result;
}

std::unordered_map<std::string, const Equipment*> EquipmentRegistry::get_name_map() const {
    std::unordered_map<std::string, const Equipment*> result;
    result.reserve(instances_.size());
    for (const auto& eq : instances_) {
        result[eq.name_id] = &eq;
    }
    return result;
}

bool EquipmentRegistry::add(const Equipment& eq) {
    if (name_to_id_.count(eq.name_id)) return false;
    int32_t idx = static_cast<int32_t>(instances_.size());
    instances_.push_back(eq);
    name_to_id_[eq.name_id] = idx;
    return true;
}

bool EquipmentRegistry::remove(const std::string& name_id) {
    auto it = name_to_id_.find(name_id);
    if (it == name_to_id_.end()) return false;
    int32_t idx = it->second;
    name_to_id_.erase(it);
    // Keep index stability for existing references; mark as invalid
    instances_[idx] = Equipment{};
    return true;
}
