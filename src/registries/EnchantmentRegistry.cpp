#include "registries/EnchantmentRegistry.h"

#include <stdexcept>

bool EnchantmentRegistry::check_validation(const std::vector<EnchInfo>& infos) {
    std::unordered_set<std::string> registration, unchecked_names;
    for (int32_t i = 0; i < static_cast<int32_t>(infos.size()); i++) {
        auto& info = infos[i];
        if (info.name_id.empty() || info.max_level <= 0 ||
            info.multiplier <= 0 || info.limited_level < 0 ||
            info.limited_level > info.max_level)
            return false;
        registration.insert(info.name_id);
        for (auto& exclusive : info.exclusive_set) {
            unchecked_names.insert(exclusive);
        }
    }
    for (auto& name_id : unchecked_names) {
        if (registration.find(name_id) == registration.end())
            return false;
    }
    return true;
}

void EnchantmentRegistry::initialize(const std::vector<EnchInfo>& infos) {
    if (!check_validation(infos))
        throw std::runtime_error("Validation check failed");
    instances_.clear();
    name_to_index_.clear();
    incompatible_table_.clear();
    local_to_global_.clear();
    global_to_local_.clear();

    instances_.reserve(infos.size());
    for (int32_t i = 0; i < static_cast<int32_t>(infos.size()); i++) {
        auto& info = infos[i];
        instances_.push_back(info);
        name_to_index_[info.name_id] = i;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(infos.size()); i++) {
        auto& info = infos[i];
        for (auto& exclusive : info.exclusive_set) {
            incompatible_table_[i].insert(name_to_index_[exclusive]);
        }
    }
}

EnchantmentRegistry EnchantmentRegistry::create_subset(const std::vector<int32_t>& global_ids) const {
    EnchantmentRegistry subset;

    subset.instances_.reserve(global_ids.size());
    subset.local_to_global_.reserve(global_ids.size());

    int32_t local_id = 0;
    for (int32_t gid : global_ids) {
        if (gid < 0 || gid >= static_cast<int32_t>(instances_.size()))
            continue;
        subset.instances_.push_back(instances_[gid]);
        subset.name_to_index_[instances_[gid].name_id] = local_id;
        subset.local_to_global_.push_back(gid);
        subset.global_to_local_[gid] = local_id;
        local_id++;
    }

    // Build remapped incompatibility table for subset
    for (int32_t li = 0; li < static_cast<int32_t>(subset.instances_.size()); li++) {
        int32_t gi = subset.local_to_global_[li];
        auto it = incompatible_table_.find(gi);
        if (it == incompatible_table_.end())
            continue;
        for (int32_t other_gi : it->second) {
            auto jt = subset.global_to_local_.find(other_gi);
            if (jt != subset.global_to_local_.end()) {
                subset.incompatible_table_[li].insert(jt->second);
            }
        }
    }

    return subset;
}

int32_t EnchantmentRegistry::to_global_id(int32_t local_id) const {
    if (local_to_global_.empty())
        return local_id;  // root registry: identity
    if (local_id < 0 || local_id >= static_cast<int32_t>(local_to_global_.size()))
        throw std::out_of_range("Local id out of range");
    return local_to_global_[local_id];
}

int32_t EnchantmentRegistry::to_local_id(int32_t global_id) const {
    if (global_to_local_.empty())
        return global_id;  // root registry: identity
    auto it = global_to_local_.find(global_id);
    if (it == global_to_local_.end())
        throw std::out_of_range("Global id not found in subset");
    return it->second;
}

const EnchInfo& EnchantmentRegistry::get(int32_t index) const {
    if (index < 0 || index >= static_cast<int32_t>(instances_.size()))
        throw std::out_of_range("EnchInfo index out of range");
    return instances_[index];
}

const EnchInfo& EnchantmentRegistry::get(const std::string& name_id) const {
    auto it = name_to_index_.find(name_id);
    if (it == name_to_index_.end())
        throw std::runtime_error("EnchInfo not found: " + name_id);
    return instances_[it->second];
}

int32_t EnchantmentRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_index_.find(name_id);
    return it != name_to_index_.end() ? it->second : -1;
}

const std::unordered_set<int32_t>& EnchantmentRegistry::get_exclusive_set(int32_t e) const {
    static const std::unordered_set<int32_t> empty_set;
    auto it = incompatible_table_.find(e);
    if (it == incompatible_table_.end())
        return empty_set;
    return it->second;
}

bool EnchantmentRegistry::is_incompatible(int32_t e1, int32_t e2) const {
    if (e1 == e2)
        return false;
    auto it = incompatible_table_.find(e1);
    if (it == incompatible_table_.end())
        return false;
    return it->second.find(e2) != it->second.end();
}
