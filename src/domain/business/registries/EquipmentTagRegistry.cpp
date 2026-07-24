#include "EquipmentTagRegistry.h"
#include <stdexcept>

EquipmentTagRegistry::EquipmentTagRegistry(const std::vector<EquipmentTag>& tags)
    : IRegistry<EquipmentTag>() {
    _data.reserve(tags.size());
    for (size_t i = 0; i < tags.size(); ++i) {
        _data.push_back(tags[i]);
        name_to_id_[tags[i].name] = static_cast<int32_t>(i);
    }
}

const EquipmentTag& EquipmentTagRegistry::get(const std::string& name) const {
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        auto msg = std::string("EquipmentTag not found: ") + name;
        throw std::out_of_range(msg.c_str());
    }
    return _data[it->second];
}

bool EquipmentTagRegistry::insert(const EquipmentTag& item) {
    if (contains(item.id))
        return false;
    name_to_id_[item.name] = static_cast<int32_t>(_data.size());
    _data.push_back(item);
    return true;
}

bool EquipmentTagRegistry::remove(const NSID& id) {
    auto it = find(id);
    if (it == _data.end())
        return false;
    size_t idx = std::distance(_data.begin(), it);
    // Remove from name map
    for (auto mit = name_to_id_.begin(); mit != name_to_id_.end(); ++mit) {
        if (mit->second == static_cast<int32_t>(idx)) {
            name_to_id_.erase(mit);
            break;
        }
    }
    _data.erase(it);
    // Shift indices for entries after the removed one
    for (auto& [_, index] : name_to_id_) {
        if (index > static_cast<int32_t>(idx))
            --index;
    }
    return true;
}

void EquipmentTagRegistry::clear() noexcept {
    _data.clear();
    name_to_id_.clear();
}
