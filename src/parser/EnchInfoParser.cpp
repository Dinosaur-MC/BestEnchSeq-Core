#include "parser/EnchInfoParser.h"
#include "parser/ParserUtils.h"
#include "io/json.h"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Platform string → enum (case-insensitive)
// ---------------------------------------------------------------------------
platform::MCE parse_platform(const std::string &str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "java" || lower == "je") {
        return platform::MCE::Java;
    }
    if (lower == "bedrock" || lower == "be") {
        return platform::MCE::Bedrock;
    }
    if (lower == "all" || lower == "both") {
        return platform::MCE::All;
    }
    std::cerr << "Warning: Unknown platform '" << str << "', defaulting to Java." << std::endl;
    return platform::MCE::Java;
}

// ---------------------------------------------------------------------------
// JSON field extraction helpers
// ---------------------------------------------------------------------------

std::string get_string_field(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return {};
    }
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::String>(val)) {
        return std::get<Json::String>(val);
    }
    return {};
}

int32_t get_int_field(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return 0;
    }
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::Number>(val)) {
        const auto &num = std::get<Json::Number>(val);
        if (std::holds_alternative<int32_t>(num)) {
            return std::get<int32_t>(num);
        }
        if (std::holds_alternative<int64_t>(num)) {
            int64_t v = std::get<int64_t>(num);
            return static_cast<int32_t>(v);
        }
    }
    return 0;
}

std::vector<std::string> get_string_array_field(const Json::Object &obj, const std::string &key) {
    std::vector<std::string> result;
    auto it = obj.find(key);
    if (it == obj.end()) {
        return result;
    }
    auto val = it->second.get_value();
    if (!std::holds_alternative<Json::Array>(val)) {
        return result;
    }
    const auto &arr = std::get<Json::Array>(val);
    result.reserve(arr.size());
    for (const auto &elem : arr) {
        auto elem_val = elem.get_value();
        if (std::holds_alternative<Json::String>(elem_val)) {
            result.push_back(std::get<Json::String>(elem_val));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Inline tag processing
// ---------------------------------------------------------------------------

// Recursively resolve a single tag value against the raw inline tag map.
void resolve_inline_value(
    const std::string &val,
    const std::unordered_map<std::string, std::vector<std::string>> &raw_tags,
    const TagResolver &tag_resolver,
    std::unordered_set<std::string> &result,
    std::unordered_set<std::string> &visiting
) {
    if (val.empty()) {
        return;
    }

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
    if (visiting.count(tag_key)) {
        return;
    }

    // Look for tag in inline raw tags first
    auto it = raw_tags.find(tag_key);
    if (it != raw_tags.end()) {
        visiting.insert(tag_key);
        for (const auto &inner_val : it->second) {
            resolve_inline_value(inner_val, raw_tags, tag_resolver, result, visiting);
        }
        visiting.erase(tag_key);
        return;
    }

    // Fall back to the resolver (tags loaded from filesystem)
    auto resolver_result = tag_resolver.resolve(val);
    result.insert(resolver_result.begin(), resolver_result.end());
}

void process_inline_tags(const Json::Object &root_obj, TagResolver &tag_resolver) {
    auto tags_it = root_obj.find("tags");
    if (tags_it == root_obj.end()) {
        return;
    }

    auto tags_val = tags_it->second.get_value();
    if (!std::holds_alternative<Json::Object>(tags_val)) {
        return;
    }
    const auto &tags_obj = std::get<Json::Object>(tags_val);

    // First pass: collect all raw tag values (including #refs)
    std::unordered_map<std::string, std::vector<std::string>> raw_tags;

    for (const auto &[category, tag_list_json] : tags_obj) {
        (void)category; // "enchantment" or "item"
        auto tag_list_val = tag_list_json.get_value();
        if (!std::holds_alternative<Json::Object>(tag_list_val)) {
            continue;
        }
        const auto &tag_list_obj = std::get<Json::Object>(tag_list_val);

        for (const auto &[tag_name, tag_value_json] : tag_list_obj) {
            auto tag_val = tag_value_json.get_value();
            if (!std::holds_alternative<Json::Object>(tag_val)) {
                continue;
            }
            const auto &tag_obj = std::get<Json::Object>(tag_val);

            auto values_it = tag_obj.find("values");
            if (values_it == tag_obj.end()) {
                continue;
            }

            auto values_val = values_it->second.get_value();
            if (!std::holds_alternative<Json::Array>(values_val)) {
                continue;
            }
            const auto &values_arr = std::get<Json::Array>(values_val);

            // Use "minecraft" as the default namespace for inline tags
            std::string key = "minecraft:" + tag_name;
            for (const auto &elem : values_arr) {
                auto elem_val = elem.get_value();
                if (std::holds_alternative<Json::String>(elem_val)) {
                    raw_tags[key].push_back(std::get<Json::String>(elem_val));
                }
            }
        }
    }

    // Second pass: resolve inter-tag references and add to resolver
    for (const auto &[key, values] : raw_tags) {
        std::unordered_set<std::string> resolved;
        std::unordered_set<std::string> visiting;

        for (const auto &val : values) {
            resolve_inline_value(val, raw_tags, tag_resolver, resolved, visiting);
        }

        tag_resolver.add_tag(key, resolved);
    }
}

// ---------------------------------------------------------------------------
// Reference resolution helper (mix of concrete IDs and #tag refs)
// ---------------------------------------------------------------------------
std::unordered_set<std::string> resolve_references(
    const std::vector<std::string> &items, TagResolver &tag_resolver
) {
    std::unordered_set<std::string> result;
    for (const auto &item : items) {
        auto expanded = tag_resolver.resolve(item);
        result.insert(expanded.begin(), expanded.end());
    }
    return result;
}

} // anonymous namespace

// ============================================================================

std::vector<EnchInfo> EnchInfoParser::parse_native_json(
    const std::filesystem::path &path,
    TagResolver &tag_resolver,
    EnchantmentDataPack *metadata
) {
    // Read and parse the JSON file
    std::string content = ParserUtils::read_file(path);
    Json root           = Json::parse(content);

    auto root_var = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) {
        return {};
    }
    const auto &root_obj = std::get<Json::Object>(root_var);

    // --- Extract metadata --------------------------------------------------
    if (metadata) {
        metadata->name        = get_string_field(root_obj, "name");
        metadata->description = get_string_field(root_obj, "description");
        metadata->author      = get_string_field(root_obj, "author");
        metadata->version     = get_string_field(root_obj, "version");
    }

    // --- Process inline tags -----------------------------------------------
    process_inline_tags(root_obj, tag_resolver);

    // --- Extract enchantments array ----------------------------------------
    auto ench_it = root_obj.find("enchantments");
    if (ench_it == root_obj.end()) {
        return {};
    }

    auto ench_val = ench_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(ench_val)) {
        return {};
    }
    const auto &ench_arr = std::get<Json::Array>(ench_val);

    // --- Parse each enchantment entry --------------------------------------
    std::vector<EnchInfo> result;
    for (const auto &ench_json : ench_arr) {
        auto elem_val = ench_json.get_value();
        if (!std::holds_alternative<Json::Object>(elem_val)) {
            continue;
        }
        const auto &elem_obj = std::get<Json::Object>(elem_val);

        // Required fields
        std::string id        = get_string_field(elem_obj, "id");
        int32_t max_level     = get_int_field(elem_obj, "max_level");
        int32_t multiplier    = get_int_field(elem_obj, "multiplier");

        if (id.empty() || max_level <= 0 || multiplier <= 0) {
            std::cerr << "Warning: Skipping enchantment entry with missing or invalid "
                         "required fields (id='"
                      << id << "', max_level=" << max_level
                      << ", multiplier=" << multiplier << ")" << std::endl;
            continue;
        }

        // Optional fields with defaults
        std::string name = get_string_field(elem_obj, "name");
        if (name.empty()) {
            name = id; // fallback to id
        }

        std::string platform_str = get_string_field(elem_obj, "platform");
        platform::MCE platform =
            platform_str.empty() ? platform::MCE::Java : parse_platform(platform_str);

        int32_t limited_level = get_int_field(elem_obj, "limited_level");
        if (limited_level <= 0) {
            limited_level = max_level;
        }

        // Exclusive set — resolve #tag references
        auto exclusive_set_items = get_string_array_field(elem_obj, "exclusive_set");
        auto exclusive_set       = resolve_references(exclusive_set_items, tag_resolver);

        // Applicable equipment — resolve #tag references
        auto equipment_items = get_string_array_field(elem_obj, "applicable_equipment");
        std::unordered_set<EquipmentCategory> applicable_equipment;
        auto resolved_equipment = resolve_references(equipment_items, tag_resolver);
        for (const auto &eq : resolved_equipment) {
            applicable_equipment.insert(EquipmentCategory(eq.c_str()));
        }

        result.emplace_back(
            std::move(id),
            std::move(name),
            platform,
            max_level,
            limited_level,
            multiplier,
            std::move(exclusive_set),
            std::move(applicable_equipment)
        );
    }

    return result;
}

// ============================================================================

std::vector<EnchInfo> EnchInfoParser::parse(
    const std::filesystem::path &path, TagResolver &tag_resolver
) {
    // Auto-detect format
    auto format = ParserUtils::detect_format(path);
    switch (format) {
    case ParserUtils::DataFormat::NativeJSON:
        return parse_native_json(path, tag_resolver);
    case ParserUtils::DataFormat::NativeCSV:
        // TODO: Task 5 — CSV format parsing
        return {};
    case ParserUtils::DataFormat::MCOfficial:
        // TODO: Task 5 — MC official format parsing
        return {};
    default:
        return {};
    }
}
