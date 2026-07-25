#include "ItemProperties.h"
#include "EmbeddedData.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"

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
            content = file_utils::read_file(fs_path);
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
        {
            auto it = io->find("durability");
            if (it != io->end()) prop.durability = it->second.as<int32_t>();
        }
        {
            auto it = io->find("enchantability");
            if (it != io->end()) prop.enchantability = it->second.as<int32_t>();
        }
        {
            auto it = io->find("category");
            if (it != io->end()) prop.category = it->second.as<std::string>();
        }

        result[item_id] = std::move(prop);
    }

    return result;
}
