#include "EnchantmentRegistry.h"

#include <stdexcept>

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

void EnchantmentRegistry::reset_for_testing() {
    instances_.clear();
    name_to_index_.clear();
    incompatible_table_.clear();
}

void EnchantmentRegistry::initialize(const std::vector<EnchInfo>& infos) {
    if (!check_validation(infos))
        throw std::runtime_error("Validation check failed");
    instances_.clear();
    name_to_index_.clear();
    incompatible_table_.clear();

    instances_.reserve(infos.size());
    for (size_t i = 0; i < infos.size(); i++) {
        auto& info = infos[i];
        instances_.push_back(info);
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

const EnchInfo& EnchantmentRegistry::get(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= instances_.size())
        throw std::out_of_range("EnchInfo index out of range");
    return instances_[index];
}

const EnchInfo& EnchantmentRegistry::get(const NSID& id) const {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        throw std::runtime_error("EnchInfo not found: " + id.str());
    return instances_[it->second];
}

int32_t EnchantmentRegistry::get_id(const NSID& id) const {
    auto it = name_to_index_.find(id);
    if (it != name_to_index_.end()) return it->second;
    // Fallback: bare NSID → try with "minecraft:" prefix
    if (id.get_ns().empty()) {
        NSID prefixed("minecraft:" + id.get_id());
        auto ns_it = name_to_index_.find(prefixed);
        if (ns_it != name_to_index_.end()) return ns_it->second;
    }
    return -1;
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

// ── Mutable operations ─────────────────────────────────────────────────────

bool EnchantmentRegistry::add(const EnchInfo& info) {
    if (name_to_index_.count(info.id))
        return false;
    int32_t idx = static_cast<int32_t>(instances_.size());
    instances_.push_back(info);
    name_to_index_[info.id] = idx;

    // Update incompatibility table
    for (const auto& excl : info.exclusive_set) {
        incompatible_table_[info.id].insert(excl);
        incompatible_table_[excl].insert(info.id);
    }
    return true;
}

bool EnchantmentRegistry::remove(const NSID& id) {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        return false;
    int32_t idx = it->second;
    name_to_index_.erase(it);
    // Keep index stability for existing references; mark as invalid
    instances_[idx] = EnchInfo{};
    return true;
}

bool EnchantmentRegistry::modify(const NSID& id, const EnchInfo& patch) {
    auto it = name_to_index_.find(id);
    if (it == name_to_index_.end())
        return false;
    EnchInfo& target = instances_[it->second];
    if (patch.max_level > 0)
        target.max_level = patch.max_level;
    if (patch.limited_level >= 0 && patch.limited_level > -1)
        target.limited_level = patch.limited_level;
    if (patch.multiplier > 0)
        target.multiplier = patch.multiplier;
    if (!patch.name.empty())
        target.name = patch.name;
    target.is_treasure = patch.is_treasure;
    return true;
}

bool EnchantmentRegistry::remove(const std::string& name_id) {
    return remove(NSID(name_id));
}

bool EnchantmentRegistry::modify(const std::string& name_id, const EnchInfo& patch) {
    return modify(NSID(name_id), patch);
}
