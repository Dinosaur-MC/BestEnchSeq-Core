#include "ItemProperties.h"
#include "EmbeddedData.h"
#include "common/io/json.h"
#include "common/utils/ParserUtils.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

std::unordered_map<std::string, ItemProperty> load_item_properties() {
    std::string content;

    // Try embedded data first (always available in release builds)
    content = std::string(besq::data::item_properties());

    if (content.empty()) {
        // Fallback: try filesystem
        auto fs_path = std::filesystem::path("data/builtin/item_properties.json");
        if (std::filesystem::exists(fs_path))
            content = ParserUtils::read_file(fs_path);
    }

    std::unordered_map<std::string, ItemProperty> result;

    if (content.empty())
        return result;  // empty map — callers handle missing entries

    Json root = Json::parse(content);
    if (root.type() != JsonType::Object)
        return result;

    auto root_val = root.get_value();
    auto* root_obj = std::get_if<Json::Object>(&root_val);
    if (!root_obj)
        return result;

    auto items_it = root_obj->find("items");
    if (items_it == root_obj->end())
        return result;

    auto items_var = items_it->second.get_value();
    auto* items_obj = std::get_if<Json::Object>(&items_var);
    if (!items_obj)
        return result;

    for (const auto& [item_id, item_json] : *items_obj) {
        auto iv = item_json.get_value();
        auto* io = std::get_if<Json::Object>(&iv);
        if (!io) continue;

        ItemProperty prop;
        prop.durability = ParserUtils::get_json_int(*io, "durability");
        prop.enchantability = ParserUtils::get_json_int(*io, "enchantability");
        prop.category = ParserUtils::get_json_string(*io, "category");

        result[item_id] = std::move(prop);
    }

    return result;
}
