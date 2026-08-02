#include "ParserShared.h"
#include "builtin/ItemProperties.h"

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

} // namespace business::parser_detail
