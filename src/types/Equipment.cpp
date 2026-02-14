#include "Equipment.h"
#include <cstdint>
#include <ranges>

bool Equipment::operator==(const Equipment &other) const { return id == other.id; }

bool Equipment::is_applicable(const std::string &ench) const {
    return applicable_enchantments.find(ench) != applicable_enchantments.end();
}
bool Equipment::is_applicable(const Ench &ench) const {
    return applicable_enchantments.find(ench.get_name()) != applicable_enchantments.end();
}

EnchSet Equipment::filter_enchantments(const EnchSet &enchantments) const {
    auto view = enchantments | std::views::filter([this](const Ench &ench) { return is_applicable(ench); });
    return EnchSet{view.begin(), view.end()};
}

std::vector<EnchInfo> Equipment::filter_enchantments(const std::vector<EnchInfo> &enchantments) const {
    auto view =
        enchantments | std::views::filter([this](const EnchInfo &ench) { return is_applicable(ench.name); });
    return std::vector<EnchInfo>{view.begin(), view.end()};
}

int32_t Equipment::merge_durability(int32_t d1, int32_t d2, int32_t max_d) {
    return std::min(max_d, static_cast<int32_t>(d1 + d2 + 0.12 * max_d));
}
int32_t Equipment::repair_durability(int32_t d, int32_t n, int32_t max_d) {
    return std::min(max_d, static_cast<int32_t>(d + 0.25 * max_d * n));
}

int32_t Equipment::calc_merge_durability(int32_t d1, int32_t d2) const {
    return merge_durability(d1, d2, max_durability);
}
int32_t Equipment::calc_repair_durability(int32_t d, int32_t n) const {
    return repair_durability(d, n, max_durability);
}
