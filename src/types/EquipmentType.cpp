#include "EquipmentType.h"

#include <ranges>

bool EquipmentType::operator==(const EquipmentType &other) const { return id == other.id; }

bool EquipmentType::is_applicable(const std::string &ench) const {
    auto set = EnchInfo::get(ench).applicable_equipment;
    return set.find(this->category) != set.end();
}
bool EquipmentType::is_applicable(const Ench &ench) const {
    auto set = ench.get_applicable_equipment();
    return set.find(this->category) != set.end();
}

EnchSet EquipmentType::filter_enchantments(const EnchSet &enchantments) const {
    auto view = enchantments | std::views::filter([this](const Ench &ench) { return is_applicable(ench); });
    return EnchSet{view.begin(), view.end()};
}
EnchInfoList EquipmentType::filter_enchantments(const EnchInfoList &enchantments) const {
    auto view =
        enchantments | std::views::filter([this](const EnchInfo &ench) { return is_applicable(ench.name); });
    return EnchInfoList{view.begin(), view.end()};
}

int32_t EquipmentType::merge_durability(int32_t d1, int32_t d2, int32_t max_d) {
    int32_t new_d = d1 + d2 + 0.12 * max_d;
    return std::min(max_d, std::max(0, new_d));
}
int32_t EquipmentType::repair_durability(int32_t d, int32_t n, int32_t max_d) {
    int32_t new_d = d + 0.25 * max_d * n;
    return std::min(max_d, std::max(0, new_d));
}
int32_t EquipmentType::calc_merge_durability(int32_t d1, int32_t d2) const {
    return merge_durability(d1, d2, max_durability);
}
int32_t EquipmentType::calc_repair_durability(int32_t d, int32_t n) const {
    return repair_durability(d, n, max_durability);
}
