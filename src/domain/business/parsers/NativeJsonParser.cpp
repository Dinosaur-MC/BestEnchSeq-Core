#include "NativeJsonParser.h"
#include "ParserShared.h"
#include "builtin/DataLoader.h"
#include "builtin/EmbeddedData.h"
#include "domain/business/components/TagResolver.h"
#include "common/log/log.hpp"

#include <unordered_set>
#include <vector>

namespace {

// ── Vanilla tag fallback ─────────────────────────────────────────────
// Seed the tag resolver with the builtin vanilla tags so a mod profile's
// `#minecraft:...` references (exclusive_set / supported_items) resolve
// against vanilla tags even when the profile does not define them.  The raw
// tags come from the single canonical extractor (override-aware) and are
// cached per process.
const std::unordered_map<std::string, std::vector<std::string>>& vanilla_raw_tags() {
    static const auto tags = [] {
        std::unordered_map<std::string, std::vector<std::string>> out;
        // Single canonical extraction (override-aware) — keeps the parser seed
        // consistent with DataLoader's base_tags / resolver seeding even when
        // data/builtin/vanilla.json is overridden on disk (T10).
        for (const auto& [key, values] : besq::data::load_builtin_tag_entries())
            out[key] = values;
        return out;
    }();
    return tags;
}

void seed_vanilla_tags(TagResolver& resolver) {
    for (const auto& [key, values] : vanilla_raw_tags()) {
        resolver.add_tag(key, std::unordered_set<std::string>(values.begin(), values.end()));
    }
}

// ── Inline tag processing ─────────────────────────────────────────────
// Recursively resolve a single tag value against the raw inline tag map.

void resolve_inline_value(
    const std::string& val,
    const std::unordered_map<std::string, std::vector<std::string>>& raw_tags,
    const TagResolver& tag_resolver,
    std::unordered_set<std::string>& result,
    std::unordered_set<std::string>& visiting
) {
    if (val.empty()) return;

    // Concrete value — return as-is
    if (val[0] != '#') {
        result.insert(val);
        return;
    }

    // Tag reference — strip '#' and determine key
    std::string tag_key = val.substr(1);
    if (tag_key.find(':') == std::string::npos) {
        tag_key = "minecraft:" + tag_key;
    }

    // Cycle detection
    if (visiting.count(tag_key)) return;

    // Look for tag in inline raw tags first
    auto it = raw_tags.find(tag_key);
    if (it != raw_tags.end()) {
        visiting.insert(tag_key);
        for (const auto& inner_val : it->second) {
            resolve_inline_value(inner_val, raw_tags, tag_resolver, result, visiting);
        }
        visiting.erase(tag_key);
        return;
    }

    // Fall back to the resolver (tags loaded from filesystem)
    auto resolver_result = tag_resolver.resolve(val);
    result.insert(resolver_result.begin(), resolver_result.end());
}

void process_inline_tags(const Json::Object& root_obj, TagResolver& tag_resolver) {
    auto tags_it = root_obj.find("tags");
    if (tags_it == root_obj.end()) return;

    auto tags_val = tags_it->second.get_value();
    if (!std::holds_alternative<Json::Object>(tags_val)) return;
    const auto& tags_obj = std::get<Json::Object>(tags_val);

    // First pass: collect all raw tag values (including #refs)
    std::unordered_map<std::string, std::vector<std::string>> raw_tags;

    for (const auto& [category, tag_list_json] : tags_obj) {
        (void)category;
        auto tag_list_val = tag_list_json.get_value();
        if (!std::holds_alternative<Json::Object>(tag_list_val)) continue;
        const auto& tag_list_obj = std::get<Json::Object>(tag_list_val);

        for (const auto& [tag_name, tag_value_json] : tag_list_obj) {
            auto tag_val = tag_value_json.get_value();

            auto collect_strings = [](const Json::Array& arr) {
                std::vector<std::string> out;
                for (const auto& elem : arr) {
                    auto val = elem.get_value();
                    if (auto* s = std::get_if<Json::String>(&val))
                        out.push_back(*s);
                }
                return out;
            };

            std::vector<std::string> raw_values;
            if (auto* arr = std::get_if<Json::Array>(&tag_val)) {
                raw_values = collect_strings(*arr);
            } else if (auto* obj = std::get_if<Json::Object>(&tag_val)) {
                auto it = obj->find("values");
                if (it != obj->end()) {
                    auto val = it->second.get_value();
                    if (auto* arr = std::get_if<Json::Array>(&val))
                        raw_values = collect_strings(*arr);
                }
            }

            if (raw_values.empty()) continue;

            // Use "minecraft" as the default namespace for inline tags
            std::string key = "minecraft:" + tag_name;
            for (auto& v : raw_values)
                raw_tags[key].push_back(std::move(v));
        }
    }

    // Second pass: resolve inter-tag references and add to resolver
    for (const auto& [key, values] : raw_tags) {
        std::unordered_set<std::string> resolved;
        std::unordered_set<std::string> visiting;

        for (const auto& val : values) {
            resolve_inline_value(val, raw_tags, tag_resolver, resolved, visiting);
        }

        tag_resolver.add_tag(key, resolved);
    }
}

// ── Parse a single enchantment entry ──────────────────────────────────

business::loader::EnchantmentData parse_ench_entry(
    const Json::Object& elem_obj,
    TagResolver& tag_resolver
) {
    using namespace business::parser_detail;

    std::string id_str, display_name;
    int32_t max_level = 0, multiplier = 0, limited_level = 0;
    std::vector<std::string> exclusive_set_items, app_items;

    {
        auto it = elem_obj.find("id");
        if (it != elem_obj.end()) id_str = it->second.as<std::string>();
    }
    {
        auto it = elem_obj.find("max_level");
        if (it != elem_obj.end()) max_level = it->second.as<int32_t>();
    }
    {
        auto it = elem_obj.find("multiplier");
        if (it != elem_obj.end()) multiplier = it->second.as<int32_t>();
    }
    {
        auto it = elem_obj.find("name");
        if (it != elem_obj.end()) display_name = it->second.as<std::string>();
    }
    if (display_name.empty()) display_name = id_str;

    {
        auto it = elem_obj.find("limited_level");
        if (it != elem_obj.end()) limited_level = it->second.as<int32_t>();
    }
    if (limited_level <= 0) limited_level = 0;

    {
        auto it = elem_obj.find("exclusive_set");
        if (it != elem_obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr)
                exclusive_set_items.push_back(elem.as<std::string>());
        }
    }
    auto exclusive_set = resolve_references(exclusive_set_items, tag_resolver);

    {
        auto it = elem_obj.find("supported_items");
        if (it != elem_obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr)
                app_items.push_back(elem.as<std::string>());
        }
    }
    business::loader::EnchantmentData ench;
    ench.id               = id_str;
    ench.display_name     = std::move(display_name);
    ench.multiplier       = multiplier;
    ench.max_level        = max_level;
    ench.limited_level    = limited_level;
    ench.exclusive_with   = std::vector<std::string>(exclusive_set.begin(), exclusive_set.end());
    // supported_items: 原始引用透传（`#tag` 或具体 ID），不展开；加载期交叉验证
    ench.applicable_to    = std::move(app_items);
    return ench;
}

// ── Parse equipment array ─────────────────────────────────────────────

std::vector<business::loader::EquipmentData> parse_equipments_json(const Json::Object& root_obj) {
    using namespace business::loader;

    auto eq_it = root_obj.find("equipments");
    if (eq_it == root_obj.end()) return {};

    auto eq_val = eq_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(eq_val)) return {};
    const auto& eq_arr = std::get<Json::Array>(eq_val);

    std::vector<EquipmentData> result;
    for (const auto& eq_json : eq_arr) {
        auto elem_val = eq_json.get_value();
        if (!std::holds_alternative<Json::Object>(elem_val)) continue;
        const auto& elem_obj = std::get<Json::Object>(elem_val);

        std::string id_str, category, name;
        int32_t max_durability = 0;

        {
            auto it = elem_obj.find("id");
            if (it != elem_obj.end()) id_str = it->second.as<std::string>();
        }
        {
            auto it = elem_obj.find("category");
            if (it != elem_obj.end()) category = it->second.as<std::string>();
        }

        if (id_str.empty() || category.empty()) {
            LOG_WARN("Warning: Skipping equipment entry with missing id or category.");
            continue;
        }

        {
            auto it = elem_obj.find("name");
            if (it != elem_obj.end()) name = it->second.as<std::string>();
        }
        if (name.empty()) name = id_str;

        {
            auto it = elem_obj.find("max_durability");
            if (it != elem_obj.end()) max_durability = it->second.as<int32_t>();
        }
        if (max_durability <= 0) max_durability = 0;

        EquipmentData eq;
        eq.id             = id_str;
        eq.display_name   = std::move(name);
        eq.category       = std::move(category);
        eq.max_durability = max_durability;
        result.push_back(std::move(eq));
    }

    return result;
}

} // anonymous namespace

// ============================================================================

NativeJsonParser::Result NativeJsonParser::parse(const Json& json) {
    TagResolver tag_resolver;

    auto root_var = json.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) return {};
    const auto& root_obj = std::get<Json::Object>(root_var);

    // Seed the resolver with vanilla tags (fallback), then overlay the
    // profile's own inline tags.
    seed_vanilla_tags(tag_resolver);
    process_inline_tags(root_obj, tag_resolver);

    // Extract enchantments
    std::vector<business::loader::EnchantmentData> enchantments;
    auto ench_it = root_obj.find("enchantments");
    if (ench_it != root_obj.end()) {
        auto ench_val = ench_it->second.get_value();
        if (std::holds_alternative<Json::Array>(ench_val)) {
            const auto& ench_arr = std::get<Json::Array>(ench_val);
            for (const auto& ench_json : ench_arr) {
                auto elem_val = ench_json.get_value();
                if (!std::holds_alternative<Json::Object>(elem_val)) continue;
                const auto& elem_obj = std::get<Json::Object>(elem_val);

                std::string id;
                int32_t max_level = 0, multiplier = 0;
                {
                    auto it = elem_obj.find("id");
                    if (it != elem_obj.end()) id = it->second.as<std::string>();
                }
                {
                    auto it = elem_obj.find("max_level");
                    if (it != elem_obj.end()) max_level = it->second.as<int32_t>();
                }
                {
                    auto it = elem_obj.find("multiplier");
                    if (it != elem_obj.end()) multiplier = it->second.as<int32_t>();
                }

                if (id.empty() || max_level <= 0 || multiplier <= 0) {
                    LOG_WARN("Warning: Skipping enchantment with missing/invalid fields "
                             "(id='%s', max_level=%d, multiplier=%d)",
                             id.c_str(), max_level, multiplier);
                    continue;
                }

                enchantments.push_back(parse_ench_entry(elem_obj, tag_resolver));
            }
        }
    }

    // Extract equipment
    auto equipment = parse_equipments_json(root_obj);

    return {std::move(enchantments), std::move(equipment)};
}

NativeJsonParser::Result NativeJsonParser::parse_string(const std::string& content) {
    return parse(Json::parse(content));
}

std::vector<std::string> NativeJsonParser::parse_categories(const Json& json) {
    std::vector<std::string> result;
    auto root_var = json.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) return result;
    const auto& root_obj = std::get<Json::Object>(root_var);

    auto it = root_obj.find("categories");
    if (it == root_obj.end()) return result;

    auto cat_val = it->second.get_value();
    if (!std::holds_alternative<Json::Array>(cat_val)) return result;
    const auto& cat_arr = std::get<Json::Array>(cat_val);

    for (const auto& elem : cat_arr) {
        auto e = elem.get_value();
        if (auto* s = std::get_if<Json::String>(&e))
            result.push_back(*s);
    }
    return result;
}

std::vector<std::string> NativeJsonParser::parse_categories_string(const std::string& content) {
    try {
        return parse_categories(Json::parse(content));
    } catch (...) {
        // Non-JSON content (e.g. a CSV builtin override) has no declared
        // categories — return empty and let the DTO parse handle the format.
        return {};
    }
}
