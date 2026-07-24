#include "EnchantmentRegistry.h"

EnchantmentRegistry::EnchantmentRegistry(const std::vector<EnchInfo>& infos) {
    _data.reserve(infos.size());
    for (const auto& info : infos)
        _data.emplace(info.id, info);
    // Build incompatibility table
    for (const auto& [id, info] : _data) {
        for (const auto& exclusive : info.exclusive_set) {
            incompatible_table_[id].insert(exclusive);
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

std::pair<EnchantmentRegistry::iterator, bool>
EnchantmentRegistry::insert(const EnchInfo& item) {
    auto [it, inserted] = _data.try_emplace(item.id, item);
    if (inserted) {
        // Build incompatibility table entries
        for (const auto& excl : item.exclusive_set) {
            incompatible_table_[item.id].insert(excl);
            incompatible_table_[excl].insert(item.id);
        }
    }
    return {iterator(it), inserted};
}

std::pair<EnchantmentRegistry::iterator, bool>
EnchantmentRegistry::insert_or_assign(const EnchInfo& item) {
    auto it = _data.find(item.id);
    if (it == _data.end()) {
        // New insertion — build incompatibility entries
        auto [new_it, inserted] = _data.emplace(item.id, item);
        for (const auto& excl : item.exclusive_set) {
            incompatible_table_[item.id].insert(excl);
            incompatible_table_[excl].insert(item.id);
        }
        return {iterator(new_it), true};
    }

    // Existing entry — rebuild incompatibility table only if exclusive_set changed
    if (it->second.exclusive_set != item.exclusive_set) {
        // Remove old entries for item.id from the table
        incompatible_table_.erase(item.id);
        for (auto& [_, excl_set] : incompatible_table_)
            excl_set.erase(item.id);
        // Add new entries based on item.exclusive_set
        for (const auto& excl : item.exclusive_set) {
            incompatible_table_[item.id].insert(excl);
            incompatible_table_[excl].insert(item.id);
        }
    }

    it->second = item;
    return {iterator(it), false};
}

bool EnchantmentRegistry::update(const EnchInfo& entry) {
    auto it = _data.find(entry.id);
    if (it == _data.end())
        return false;

    // Rebuild incompatibility entries for this entry
    incompatible_table_.erase(entry.id);
    for (auto& [_, excl_set] : incompatible_table_)
        excl_set.erase(entry.id);
    for (const auto& excl : entry.exclusive_set) {
        incompatible_table_[entry.id].insert(excl);
        incompatible_table_[excl].insert(entry.id);
    }

    it->second = entry;
    return true;
}

bool EnchantmentRegistry::erase(const NSID& id) {
    auto it = _data.find(id);
    if (it == _data.end())
        return false;

    // Remove from incompatibility table
    incompatible_table_.erase(it->first);
    for (auto& [_, excl_set] : incompatible_table_)
        excl_set.erase(it->first);

    _data.erase(it);
    return true;
}

void EnchantmentRegistry::clear() noexcept {
    _data.clear();
    incompatible_table_.clear();
}
