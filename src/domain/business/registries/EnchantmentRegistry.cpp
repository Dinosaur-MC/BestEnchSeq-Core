#include "EnchantmentRegistry.h"

#include <stdexcept>

EnchantmentRegistry::EnchantmentRegistry(const std::vector<EnchInfo>& infos) {
    _data.reserve(infos.size());
    for (size_t i = 0; i < infos.size(); i++) {
        auto& info = infos[i];
        _data.push_back(info);
        name_to_index_[info.id] = static_cast<int32_t>(i);
    }
    for (size_t i = 0; i < infos.size(); i++) {
        auto& info = infos[i];
        for (auto& exclusive : info.exclusive_set) {
            incompatible_table_[info.id].insert(exclusive);
            incompatible_table_[exclusive].insert(info.id);
        }
    }
}

bool EnchantmentRegistry::check_validation(const std::vector<EnchInfo>& infos) {
    std::unordered_set<NSID> registration, unchecked_names;
    for (size_t i = 0; i < infos.size(); i++) {
        auto& info = infos[i];
        if (info.id.empty() || info.max_level <= 0 ||
            info.multiplier <= 0 || info.limited_level < 0 ||
            info.limited_level > info.max_level)
            return false;
        registration.insert(info.id);
        for (auto& exclusive : info.exclusive_set) {
            unchecked_names.insert(exclusive);
        }
    }
    for (auto& nsid : unchecked_names) {
        if (registration.find(nsid) == registration.end())
            return false;
    }
    return true;
}

const EnchInfo& EnchantmentRegistry::get(int32_t id) const {
    return _data.at(static_cast<size_t>(id));
}

const EnchInfo& EnchantmentRegistry::get(const NSID& id) const {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        throw std::out_of_range("Enchantment not found: " + id.str());
    return _data[it->second];
}

const std::unordered_set<NSID>& EnchantmentRegistry::get_exclusive_set(const NSID& e) const {
    static const std::unordered_set<NSID> empty_set;
    auto it = incompatible_table_.find(e);
    if (it == incompatible_table_.end())
        return empty_set;
    return it->second;
}

bool EnchantmentRegistry::is_incompatible(const NSID& e1, const NSID& e2) const {
    if (e1 == e2)
        return false;
    auto it = incompatible_table_.find(e1);
    if (it == incompatible_table_.end())
        return false;
    return it->second.find(e2) != it->second.end();
}

// ── IRegistry overrides ────────────────────────────────────────────────

EnchantmentRegistry::iterator EnchantmentRegistry::find(const NSID& id) {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        return _data.end();
    return _data.begin() + it->second;
}

EnchantmentRegistry::const_iterator EnchantmentRegistry::find(const NSID& id) const {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        return _data.end();
    return _data.begin() + it->second;
}

bool EnchantmentRegistry::insert(const EnchInfo& item) {
    if (name_to_index_.count(item.id))
        return false;

    int32_t idx = static_cast<int32_t>(_data.size());
    name_to_index_[item.id] = idx;
    _data.push_back(item);

    // Build incompatibility table entries
    for (const auto& excl : item.exclusive_set) {
        incompatible_table_[item.id].insert(excl);
        incompatible_table_[excl].insert(item.id);
    }
    return true;
}

bool EnchantmentRegistry::remove(const NSID& id) {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        return false;

    int32_t idx = it->second;
    const NSID& removed_id = id;

    // Remove from incompatibility table
    incompatible_table_.erase(removed_id);
    for (auto& [_, excl_set] : incompatible_table_)
        excl_set.erase(removed_id);

    // Remove from index map
    name_to_index_.erase(it);

    // Remove from data
    _data.erase(_data.begin() + idx);

    // Shift indices
    for (auto& [_, index] : name_to_index_)
        if (index > idx)
            --index;

    return true;
}

void EnchantmentRegistry::clear() noexcept {
    _data.clear();
    name_to_index_.clear();
    incompatible_table_.clear();
}
