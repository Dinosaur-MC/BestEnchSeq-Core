#include "Equipment.h"

#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"

#include <ranges>

bool Equipment::operator==(const Equipment& other) const { return name_id == other.name_id; }

bool Equipment::is_applicable(const std::string& ench) const {
    const auto& info = EnchantmentRegistry::get_instance().get(ench);
    auto& set = info.applicable_category_ids;
    return set.find(category_id) != set.end() ||
           set.find(EquipmentCategoryRegistry::ID_ANY) != set.end();
}

bool Equipment::is_applicable(const Ench& ench) const {
    auto& set = ench.get_applicable_equipment();
    return set.find(category_id) != set.end() ||
           set.find(EquipmentCategoryRegistry::ID_ANY) != set.end();
}

EnchSet Equipment::filter_enchantments(const EnchSet& enchantments) const {
    auto view = enchantments | std::views::filter([this](const Ench& ench) { return is_applicable(ench); });
    return EnchSet{view.begin(), view.end()};
}

EnchInfoList Equipment::filter_enchantments(const EnchInfoList& enchantments) const {
    auto view = enchantments | std::views::filter([this](const EnchInfo& ench) { return is_applicable(ench.name_id); });
    return EnchInfoList{view.begin(), view.end()};
}

// ── Durability ──

int32_t Equipment::merge_durability(int32_t d1, int32_t d2, int32_t max_d) {
    int32_t new_d = d1 + d2 + 12 * max_d / 100;
    return std::min(max_d, std::max(0, new_d));
}

int32_t Equipment::repair_durability(int32_t d, int32_t n, int32_t max_d) {
    int32_t new_d = d + 25 * max_d * n / 100;
    return std::min(max_d, std::max(0, new_d));
}

int32_t Equipment::calc_merge_durability(int32_t d1, int32_t d2) const {
    return merge_durability(d1, d2, max_durability);
}

int32_t Equipment::calc_repair_durability(int32_t d, int32_t n) const {
    return repair_durability(d, n, max_durability);
}
