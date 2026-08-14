#include "NativeJsonParser.h"
#include "ParserShared.h"
#include "domain/business/loaders/BuiltinData.h"
#include "builtin/EmbeddedData.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/schemas/EnchInfoSchema.h"
#include "domain/business/schemas/EquipmentSchema.h"
#include "ds/ds.h"
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
    const Json& elem,
    TagResolver& tag_resolver
) {
    using namespace business::parser_detail;
    using EnchDataJson = business::schema::EnchantmentDataJson;

    business::loader::EnchantmentData ench;
    ds::ErrorList err;
    if (!EnchDataJson::parse(elem, ench, err)) {
        LOG_WARN("Enchantment parse errors: %s", err.str().c_str());
        // 仍用已解析的部分字段走下方跳过检查（与旧行为一致：容错）
    }
    if (ench.display_name.empty()) ench.display_name = ench.id;
    // exclusive_set：tag 引用展开（#ref → 具体魔咒 ID）；supported_items 原样透传
    auto resolved = resolve_references(ench.exclusive_with, tag_resolver);
    ench.exclusive_with.assign(resolved.begin(), resolved.end());
    return ench;
}

// ── Parse equipment array ─────────────────────────────────────────────

std::vector<business::loader::EquipmentData> parse_equipments_json(const Json::Object& root_obj) {
    using namespace business::loader;
    using EqDataJson = business::schema::EquipmentDataJson;

    auto eq_it = root_obj.find("equipments");
    if (eq_it == root_obj.end()) return {};
    auto eq_val = eq_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(eq_val)) return {};
    const auto& eq_arr = std::get<Json::Array>(eq_val);

    std::vector<EquipmentData> result;
    for (const auto& eq_json : eq_arr) {
        EquipmentData eq;
        ds::ErrorList err;
        if (!EqDataJson::parse(eq_json, eq, err)) {
            LOG_WARN("Equipment parse errors: %s", err.str().c_str());
        }
        if (eq.id.empty() || eq.category.empty()) {
            LOG_WARN("Warning: Skipping equipment entry with missing id or category.");
            continue;
        }
        if (eq.display_name.empty()) eq.display_name = eq.id;
        if (eq.max_durability <= 0) eq.max_durability = 0;
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

                auto ench = parse_ench_entry(ench_json, tag_resolver);

                // 跳过逻辑：从 parse_ench_entry 返回的 DTO 读字段（schema 解析容错，
                // 缺失/类型错时字段保持默认值），语义与旧手写读取一致。
                if (ench.id.empty() || ench.max_level <= 0 || ench.multiplier <= 0) {
                    LOG_WARN("Warning: Skipping enchantment with missing/invalid fields "
                             "(id='%s', max_level=%d, multiplier=%d)",
                             ench.id.c_str(), ench.max_level, ench.multiplier);
                    continue;
                }

                enchantments.push_back(std::move(ench));
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
