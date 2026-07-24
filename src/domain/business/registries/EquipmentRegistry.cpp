#include "EquipmentRegistry.h"
#include <stdexcept>

EquipmentRegistry::EquipmentRegistry(const std::vector<Equipment>& eq_list) {
    _data.reserve(eq_list.size());
    for (const auto& eq : eq_list) {
        _data.push_back(eq);
        name_to_id_[eq.id] = static_cast<int32_t>(_data.size()) - 1;
    }
}

const Equipment& EquipmentRegistry::get(int32_t id) const {
    return _data.at(static_cast<size_t>(id));
}

const Equipment& EquipmentRegistry::get(const NSID& id) const {
    auto it = name_to_id_.find(id);
    if (it == name_to_id_.end())
        throw std::out_of_range("Equipment not found: " + id.str());
    return _data[it->second];
}

std::vector<const Equipment*> EquipmentRegistry::get_by_category(const NSID& category) const {
    std::vector<const Equipment*> result;
    for (const auto& eq : _data)
        if (eq.category == category)
            result.push_back(&eq);
    return result;
}

std::unordered_map<NSID, const Equipment*> EquipmentRegistry::get_name_map() const {
    std::unordered_map<NSID, const Equipment*> map;
    map.reserve(_data.size());
    for (const auto& eq : _data)
        map[eq.id] = &eq;
    return map;
}

// ── IRegistry overrides ────────────────────────────────────────────────

EquipmentRegistry::iterator EquipmentRegistry::find(const NSID& id) {
    auto it = name_to_id_.find(id);
    if (it == name_to_id_.end())
        return _data.end();
    return _data.begin() + it->second;
}

EquipmentRegistry::const_iterator EquipmentRegistry::find(const NSID& id) const {
    auto it = name_to_id_.find(id);
    if (it == name_to_id_.end())
        return _data.end();
    return _data.begin() + it->second;
}

bool EquipmentRegistry::insert(const Equipment& item) {
    if (name_to_id_.count(item.id))
        return false;
    name_to_id_[item.id] = static_cast<int32_t>(_data.size());
    _data.push_back(item);
    return true;
}

bool EquipmentRegistry::remove(const NSID& id) {
    auto it = name_to_id_.find(id);
    if (it == name_to_id_.end())
        return false;
    int32_t idx = it->second;
    name_to_id_.erase(it);
    _data.erase(_data.begin() + idx);
    // Shift indices of entries after the removed one
    for (auto& [_, index] : name_to_id_)
        if (index > idx)
            --index;
    return true;
}

void EquipmentRegistry::clear() noexcept {
    _data.clear();
    name_to_id_.clear();
}
