#include "EnchInfo.h"

#include <ranges>
#include <stdexcept>

std::vector<EnchInfo> EnchInfo::instances;
std::unordered_map<std::string, int32_t> EnchInfo::name_to_index;
std::unordered_map<int32_t, std::unordered_set<int32_t>> EnchInfo::incompatible_table;

platform::MCE EnchInfo::active_platform = platform::MCE::Java;

bool EnchInfo::check_validation(const std::vector<EnchInfo> &infos) {
    std::unordered_set<std::string> registration, unchecked_names;
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        if (info.name_id.empty() || info.max_level <= 0 || info.multiplier <= 0 || info.limited_level < 0 ||
            info.limited_level > info.max_level)
            return false;
        registration.insert(info.name_id);
        for (auto &exclusive : info.exclusive_set) {
            unchecked_names.insert(exclusive);
        }
    }
    for (auto &name_id : unchecked_names) {
        if (registration.find(name_id) == registration.end()) {
            return false;
        }
    }
    return true;
}

bool EnchInfo::operator==(const EnchInfo &other) const { return name_id == other.name_id; }

std::vector<EnchInfo> EnchInfo::preprocess(const std::vector<EnchInfo> &infos) {
    std::unordered_set<std::string> registration;
    for (auto &info : infos)
        registration.insert(info.name_id);

    auto filter = [&registration](const std::unordered_set<std::string> &exclusive_set) {
        return exclusive_set | std::views::filter([&registration](const std::string &name) {
                   return registration.contains(name);
               });
    };

    std::vector<EnchInfo> new_infos;
    for (auto &info : infos) {
        auto view = filter(info.exclusive_set);
        new_infos.push_back(EnchInfo({
            info.name_id,
            info.name,
            info.supported_platform,
            info.max_level,
            info.limited_level,
            info.multiplier,
            {view.begin(), view.end()},
        }));
    }
    return new_infos;
}
void EnchInfo::initialize(const std::vector<EnchInfo> &infos) {
    if (!check_validation(infos))
        throw std::runtime_error("Validation check failed");
    instances.clear();
    name_to_index.clear();
    instances.reserve(infos.size());
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        instances.push_back(info);
        name_to_index[info.name_id] = i;
    }
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        for (auto &exclusive : info.exclusive_set) {
            incompatible_table[i].insert(name_to_index[exclusive]);
        }
    }
}
const std::vector<EnchInfo> &EnchInfo::get_instances() { return instances; }

const EnchInfo &EnchInfo::get(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(instances.size())) {
        throw std::out_of_range("EnchInfo index out of range");
    }
    return instances[index];
}
const EnchInfo &EnchInfo::get(const std::string &name_id) {
    auto it = name_to_index.find(name_id);
    if (it == name_to_index.end()) {
        throw std::runtime_error("EnchInfo not found: " + name_id);
    }
    return instances[it->second];
}
int32_t EnchInfo::get_id(const std::string &name_id) {
    auto it = name_to_index.find(name_id);
    return it != name_to_index.end() ? it->second : -1;
}
void EnchInfo::set_active_platform(platform::MCE type) { active_platform = type; }
platform::MCE EnchInfo::get_active_platform() { return active_platform; }

const std::unordered_set<int32_t> &EnchInfo::get_exclusive_set(int32_t e) {
    static const std::unordered_set<int32_t> empty_set;
    auto it = incompatible_table.find(e);
    if (it == incompatible_table.end())
        return empty_set;
    return it->second;
}

bool EnchInfo::is_incompatible(int32_t e1, int32_t e2) {
    if (e1 == e2)
        return false;
    auto it = incompatible_table.find(e1);
    if (it == incompatible_table.end())
        return false;
    return it->second.find(e2) != it->second.end();
}
