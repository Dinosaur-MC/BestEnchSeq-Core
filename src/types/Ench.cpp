#include "Ench.h"
#include "registries/EnchantmentRegistry.h"

#include <algorithm>
#include <stdexcept>

bool Ench::operator==(const Ench& other) const { return id == other.id; }
int32_t Ench::operator+(int32_t lvl) const {
    return std::min(level == lvl ? level + 1 : std::max(level, lvl), get_max_level());
}
int32_t Ench::operator+=(int32_t lvl) {
    return level = std::min(level == lvl ? level + 1 : std::max(level, lvl), get_max_level());
}
Ench Ench::operator+(const Ench& other) const {
    if (id != other.id) return *this;
    return Ench(id, std::min(level == other.level ? level + 1 : std::max(level, other.level),
                             get_max_level()), unchecked);
}
Ench& Ench::operator+=(const Ench& other) {
    if (id == other.id)
        level = std::min(level == other.level ? level + 1 : std::max(level, other.level),
                         get_max_level());
    return *this;
}

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
platform::MCE Ench::get_supported_platform() const { return EnchantmentRegistry::get_instance().get(id).supported_platform; }
int32_t Ench::get_max_level() const { return EnchantmentRegistry::get_instance().get(id).max_level; }
int32_t Ench::get_limited_level() const { return EnchantmentRegistry::get_instance().get(id).limited_level; }
int32_t Ench::get_multiplier(bool is_book) const {
    return is_book ? std::max(1, EnchantmentRegistry::get_instance().get(id).multiplier >> 1)
                   : EnchantmentRegistry::get_instance().get(id).multiplier;
}
const std::unordered_set<int32_t>& Ench::get_exclusive_set() const {
    return EnchantmentRegistry::get_instance().get_exclusive_set(id);
}
const std::unordered_set<int32_t>& Ench::get_applicable_equipment() const {
    return EnchantmentRegistry::get_instance().get(id).applicable_category_ids;
}

bool Ench::is_incompatible(const Ench& other) const {
    return EnchantmentRegistry::get_instance().is_incompatible(id, other.id);
}
