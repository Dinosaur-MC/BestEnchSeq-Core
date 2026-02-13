#include "Ench.h"
#include "EnchInfo.h"

#include <stdexcept>

bool Ench::operator==(const Ench &other) const { return id == other.id; }

Ench::Ench(int32_t id) : id(id), lvl(1) {
    if (id < 0 || id >= static_cast<int32_t>(EnchInfo::get_instances().size())) {
        throw std::out_of_range("Invalid Ench id");
    }
}
Ench::Ench(int32_t id, int32_t lvl) : id(id), lvl(lvl) {
    if (id < 0 || id >= static_cast<int32_t>(EnchInfo::get_instances().size())) {
        throw std::out_of_range("Invalid Ench id");
    }
}

std::string Ench::get_name() const { return EnchInfo::get(id).name; }
MCE Ench::get_supported_platform() const { return EnchInfo::get(id).supported_platform; }
int32_t Ench::get_max_level() const { return EnchInfo::get(id).max_level; }
int32_t Ench::get_current_multiplier() const {
    const auto &info     = EnchInfo::get(id);
    auto active_platform = EnchInfo::get_active_platform();

    // 检查当前类型是否被支持
    if (static_cast<int32_t>(info.supported_platform) & static_cast<int32_t>(active_platform)) {
        return info.multiplier[static_cast<size_t>(active_platform)];
    }
    return 0; // 如果不支持当前类型，返回0
}
const std::unordered_set<int32_t> &Ench::get_incompatible() const { return EnchInfo::get_incompatible(id); }

bool Ench::is_incompatible(const Ench &other) const { return EnchInfo::is_incompatible(id, other.id); }
