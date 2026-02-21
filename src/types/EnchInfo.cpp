#include "EnchInfo.h"

#include <stdexcept>

std::vector<EnchInfo> EnchInfo::instances;
std::unordered_map<std::string, int32_t> EnchInfo::name_to_index;
std::unordered_map<int32_t, std::unordered_set<int32_t>> EnchInfo::incompatible_table;

MCE EnchInfo::active_platform = MCE::Java;

bool EnchInfo::check_validation(const std::vector<EnchInfo> &infos) {
    std::unordered_set<std::string> registration, unchecked_names;
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        registration.insert(info.name);
        for (auto &incomp : info.exclusive_set) {
            unchecked_names.insert(incomp);
        }
    }
    for (auto &name : unchecked_names) {
        if (registration.find(name) == registration.end()) {
            return false;
        }
    }
    return true;
}

bool EnchInfo::operator==(const EnchInfo &other) const { return name == other.name; }

void EnchInfo::initialize(const std::vector<EnchInfo> &infos) {
    if (!check_validation(infos))
        throw std::runtime_error("Validation check failed");
    instances.clear();
    name_to_index.clear();
    instances.reserve(instances.size());
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        instances.push_back(info);
        name_to_index[info.name] = i;
    }
    for (int32_t i = 0; i < infos.size(); i++) {
        auto &info = infos[i];
        for (auto &incomp : info.exclusive_set) {
            incompatible_table[i].insert(name_to_index[incomp]);
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
const EnchInfo &EnchInfo::get(const std::string &name) {
    auto it = name_to_index.find(name);
    if (it == name_to_index.end()) {
        throw std::runtime_error("EnchInfo not found: " + name);
    }
    return instances[it->second];
}
int32_t EnchInfo::get_id(const std::string &name) {
    return name_to_index.find(name) != name_to_index.end() ? name_to_index[name] : -1;
}
void EnchInfo::set_active_platform(MCE type) { active_platform = type; }
MCE EnchInfo::get_active_platform() { return active_platform; }

const std::unordered_set<int32_t> &EnchInfo::get_incompatible(int32_t e) {
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
