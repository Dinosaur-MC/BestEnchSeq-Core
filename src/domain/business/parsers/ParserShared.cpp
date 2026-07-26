#include "ParserShared.h"
#include "builtin/ItemProperties.h"

#include <cmath>

namespace business::parser_detail {

std::string get_category_suffix(const std::string& item_id) {
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos)
        key = key.substr(colon + 1);

    static const std::unordered_map<std::string, std::string> suffix_to_category = {
        {"_sword", "sword"},
        {"_pickaxe", "pickaxe"},
        {"_axe", "axe"},
        {"_shovel", "shovel"},
        {"_hoe", "hoe"},
        {"_helmet", "helmet"},
        {"_chestplate", "chestplate"},
        {"_leggings", "leggings"},
        {"_boots", "boots"},
        {"_horse_armor", "horse_armor"},
        {"bow", "bow"},
        {"crossbow", "crossbow"},
        {"trident", "trident"},
        {"shield", "shield"},
        {"fishing_rod", "fishing_rod"},
        {"elytra", "elytra"},
        {"_skull", "skull"},
        {"_head", "head"},
        {"mace", "mace"},
        {"brush", "brush"},
    };

    for (const auto& [suffix, cat] : suffix_to_category) {
        if (key == suffix ||
            (key.size() > suffix.size() && key.substr(key.size() - suffix.size()) == suffix)) {
            return cat;
        }
    }
    return key;
}

int32_t compute_limited_level(
    int32_t max_level,
    int32_t min_cost_base,
    int32_t min_cost_per_level,
    const std::unordered_set<std::string>& applicable_items,
    const std::unordered_map<std::string, ItemProperty>& item_props
) {
    auto max_power = [](int32_t enchantability) -> int32_t {
        if (enchantability <= 0)
            return 0;
        double base = 30.0;
        double added = 1.0 + 2.0 * (static_cast<double>(enchantability) / 4.0);
        return static_cast<int32_t>(std::round((base + added) * 1.15));
    };

    int32_t best = 0;
    for (const auto& item : applicable_items) {
        std::string bare = item;
        auto colon = bare.find(':');
        if (colon != std::string::npos)
            bare = bare.substr(colon + 1);

        auto it = item_props.find(bare);
        if (it == item_props.end() || it->second.enchantability <= 0)
            continue;

        int32_t power = max_power(it->second.enchantability);
        if (power >= min_cost_base) {
            int32_t max_lvl = (power - min_cost_base) / min_cost_per_level + 1;
            if (max_lvl > max_level)
                max_lvl = max_level;
            if (max_lvl > best)
                best = max_lvl;
        }
    }
    return std::max<int32_t>(1, best);
}

} // namespace business::parser_detail
