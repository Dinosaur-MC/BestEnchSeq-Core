#include "Ench.h"
#include "registries/EnchantmentRegistry.h"

#include <stdexcept>

Ench::Ench(int32_t id) : id(id), level(1) {
    if (id < 0 || id >= static_cast<int32_t>(EnchantmentRegistry::get_instance().size()))
        throw std::out_of_range("Invalid Ench id");
}
Ench::Ench(int32_t id, int32_t level) : id(id), level(level) {
    if (id < 0 || id >= static_cast<int32_t>(EnchantmentRegistry::get_instance().size()))
        throw std::out_of_range("Invalid Ench id");
}

Ench Ench::checked(int32_t id, int32_t level, const EnchantmentRegistry& reg) {
    if (id < 0 || id >= static_cast<int32_t>(reg.size()))
        throw std::out_of_range("Invalid Ench id");
    return Ench(id, level, unchecked);
}

std::string Ench::get_name() const { return EnchantmentRegistry::get_instance().get(id).name_id; }
MCE Ench::get_supported_platform() const { return EnchantmentRegistry::get_instance().get(id).supported_platform; }
int32_t Ench::get_max_level() const { return EnchantmentRegistry::get_instance().get(id).max_level; }
int32_t Ench::get_limited_level() const { return EnchantmentRegistry::get_instance().get(id).limited_level; }

const std::unordered_set<int32_t>& Ench::get_exclusive_set() const {
    return EnchantmentRegistry::get_instance().get_exclusive_set(id);
}
const std::unordered_set<int32_t>& Ench::get_applicable_equipment() const {
    return EnchantmentRegistry::get_instance().get(id).applicable_category_ids;
}

bool Ench::is_incompatible(const Ench& other) const {
    return EnchantmentRegistry::get_instance().is_incompatible(id, other.id);
}
