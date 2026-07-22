#include "../registries/EnchReg.h"
#include "InventoryResolver.h"
#include "common/io/json.h"
#include "common/utils/ParserUtils.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace algorithm {
InventoryInput InventoryResolver::resolve(
    const std::filesystem::path &path, const EnchantmentRegistry &ench_reg, const EquipmentRegistry &eq_reg
) {
    InventoryInput result;

    std::string content = ParserUtils::read_file(path);
    Json root           = Json::parse(content);
    if (root.type() != JsonType::Object)
        return result;

    Json::Value root_val = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_val))
        return result;
    Json::Object root_obj = std::get<Json::Object>(root_val);

    auto items_it = root_obj.find("items");
    if (items_it == root_obj.end())
        return result;

    Json::Value items_var = items_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(items_var))
        return result;
    Json::Array items_arr = std::get<Json::Array>(items_var);

    for (const Json &item_json : items_arr) {
        if (item_json.type() != JsonType::Object)
            continue;
        Json::Value item_val = item_json.get_value();
        if (!std::holds_alternative<Json::Object>(item_val))
            continue;
        Json::Object item_obj = std::get<Json::Object>(item_val);

        std::string type = ParserUtils::get_json_string(item_obj, "type");
        if (type.empty()) {
            result.warnings.push_back("item missing 'type' field, skipping");
            continue;
        }

        // Parse enchantments
        EnchSet ench_set;
        auto ench_it = item_obj.find("enchants");
        if (ench_it != item_obj.end()) {
            Json::Value enchants_val = ench_it->second.get_value();
            if (std::holds_alternative<Json::Array>(enchants_val)) {
                Json::Array ench_arr = std::get<Json::Array>(enchants_val);
                for (const Json &ench_json : ench_arr) {
                    if (ench_json.type() != JsonType::Object)
                        continue;
                    Json::Value ench_val = ench_json.get_value();
                    if (!std::holds_alternative<Json::Object>(ench_val))
                        continue;
                    Json::Object ench_obj = std::get<Json::Object>(ench_val);

                    std::string eid = ParserUtils::get_json_string(ench_obj, "id");
                    int32_t level   = ParserUtils::get_json_int(ench_obj, "level");
                    if (level < 1)
                        level = 1;

                    int32_t id = ench_reg.get_id(eid);
                    if (id >= 0) {
                        ench_set.emplace(id, level);
                    } else {
                        result.warnings.push_back("unknown enchantment '" + eid + "'");
                    }
                }
            }
        }

        int32_t prior_penalty = ParserUtils::get_json_int(item_obj, "prior_penalty");
        int32_t priority      = ParserUtils::get_json_int(item_obj, "priority");
        if (priority <= 0)
            priority = 99;

        if (type == "book") {
            auto &item    = result.items.emplace_back(ench_set, prior_penalty);
            item.priority = priority;
        } else if (type == "equipment") {
            std::string equip_id = ParserUtils::get_json_string(item_obj, "id");
            int32_t eq_id        = eq_reg.get_id(equip_id);
            if (eq_id >= 0) {
                const Equipment &equip = eq_reg.get(eq_id);
                int32_t durability     = ParserUtils::get_json_int(item_obj, "durability");
                if (durability <= 0)
                    durability = equip.max_durability;
                auto &item    = result.items.emplace_back(equip, ench_set, prior_penalty, durability);
                item.priority = priority;
            } else {
                result.warnings.push_back("unknown equipment '" + equip_id + "', treating as book");
                auto &item    = result.items.emplace_back(ench_set, prior_penalty);
                item.priority = priority;
            }
        } else {
            result.warnings.push_back("unknown item type '" + type + "', skipping");
        }
    }

    // Stable sort by priority (lower = more preferred)
    std::stable_sort(result.items.begin(), result.items.end(), [](const Item &a, const Item &b) {
        return a.priority < b.priority;
    });

    return result;
}
} // namespace algorithm
