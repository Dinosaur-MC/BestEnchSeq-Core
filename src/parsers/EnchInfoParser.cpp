#include "parsers/EnchInfoParser.h"
#include "utils/ParserUtils.h"
#include "log/log.hpp"
#include "io/CsvIO.h"
#include "io/json.h"

#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

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

            // Support two formats:
            //   1) "tag_name": ["value1", "value2"]        — flat array
            //   2) "tag_name": {"values": ["value1", ...]}  — explicit object
            // Extract tag values into a local vector, then move into raw_tags.
            auto collect_strings = [](const Json::Array &arr) {
                std::vector<std::string> out;
                for (const auto &elem : arr) {
                    auto val = elem.get_value();
                    if (auto *s = std::get_if<Json::String>(&val))
                        out.push_back(*s);
                }
                return out;
            };

            std::vector<std::string> raw_values;
            if (auto *arr = std::get_if<Json::Array>(&tag_val)) {
                raw_values = collect_strings(*arr);
            } else if (auto *obj = std::get_if<Json::Object>(&tag_val)) {
                auto it = obj->find("values");
                if (it != obj->end()) {
                    auto val = it->second.get_value();
                    if (auto *arr = std::get_if<Json::Array>(&val))
                        raw_values = collect_strings(*arr);
                }
            }

            if (raw_values.empty()) continue;

            // Use "minecraft" as the default namespace for inline tags
            std::string key = "minecraft:" + tag_name;
            for (auto &v : raw_values)
                raw_tags[key].push_back(std::move(v));
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

std::vector<RawEnchInfo> EnchInfoParser::parse_native_json(
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
        metadata->name        = ParserUtils::get_json_string(root_obj, "name");
        metadata->description = ParserUtils::get_json_string(root_obj, "description");
        metadata->author      = ParserUtils::get_json_string(root_obj, "author");
        metadata->version     = ParserUtils::get_json_string(root_obj, "version");
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
    std::vector<RawEnchInfo> result;
    for (const auto &ench_json : ench_arr) {
        auto elem_val = ench_json.get_value();
        if (!std::holds_alternative<Json::Object>(elem_val)) {
            continue;
        }
        const auto &elem_obj = std::get<Json::Object>(elem_val);

        // Required fields
        std::string id        = ParserUtils::get_json_string(elem_obj, "id");
        int32_t max_level     = ParserUtils::get_json_int(elem_obj, "max_level");
        int32_t multiplier    = ParserUtils::get_json_int(elem_obj, "multiplier");

        if (id.empty() || max_level <= 0 || multiplier <= 0) {
            LOG_WARN("Warning: Skipping enchantment entry with missing or invalid required fields (id='%s', max_level=%d, multiplier=%d)", id.c_str(), max_level, multiplier);
            continue;
        }

        // Optional fields with defaults
        std::string name = ParserUtils::get_json_string(elem_obj, "name");
        if (name.empty()) {
            name = id; // fallback to id
        }

        std::string platform_str = ParserUtils::get_json_string(elem_obj, "platform");
        MCE platform =
            platform_str.empty() ? MCE::Java : ParserUtils::parse_platform(platform_str);

        int32_t limited_level = ParserUtils::get_json_int(elem_obj, "limited_level");
        if (limited_level <= 0) {
            limited_level = max_level;
        }

        // Treasure enchantment flag (optional, defaults to false)
        bool is_treasure = ParserUtils::get_json_bool(elem_obj, "is_treasure");

        // Exclusive set — resolve #tag references
        auto exclusive_set_items = ParserUtils::get_json_string_array(elem_obj, "exclusive_set");
        auto exclusive_set       = resolve_references(exclusive_set_items, tag_resolver);

        // Applicable equipment — resolve #tag references, keep as strings
        auto equipment_items = ParserUtils::get_json_string_array(elem_obj, "applicable_equipment");
        auto applicable_equipment = resolve_references(equipment_items, tag_resolver);

        result.emplace_back(RawEnchInfo{
            std::move(id),
            std::move(name),
            platform,
            max_level,
            limited_level,
            multiplier,
            is_treasure,
            std::move(exclusive_set),
            std::move(applicable_equipment)
        });
    }

    return result;
}

// ============================================================================

std::vector<RawEnchInfo> EnchInfoParser::parse(
    const std::filesystem::path &path, TagResolver &tag_resolver
) {
    // Auto-detect format
    auto format = ParserUtils::detect_format(path);
    switch (format) {
    case ParserUtils::DataFormat::NativeJSON:
        return parse_native_json(path, tag_resolver);
    case ParserUtils::DataFormat::NativeCSV:
        return parse_native_csv(path, tag_resolver);
    case ParserUtils::DataFormat::MCOfficial:
        return parse_mc_official(path, tag_resolver);
    default:
        throw std::runtime_error("Unknown format: " + path.string());
    }
}

// ============================================================================

std::vector<RawEnchInfo> EnchInfoParser::parse_native_csv(
    const std::filesystem::path &path, TagResolver &tag_resolver
) {
    auto rows = csv::parse(path);
    if (rows.empty()) {
        return {};
    }

    // First row is header — map column names to indices
    const auto &header = rows[0];
    std::unordered_map<std::string, size_t> col_index;
    for (size_t i = 0; i < header.size(); ++i) {
        col_index[header[i]] = i;
    }

    // Verify required columns exist
    auto req_id     = col_index.find("id");
    auto req_max    = col_index.find("max_level");
    auto req_mult   = col_index.find("multiplier");
    if (req_id == col_index.end() || req_max == col_index.end() ||
        req_mult == col_index.end()) {
        LOG_WARN("Warning: CSV file missing required columns (id, max_level, multiplier).");
        return {};
    }

    // Helper to extract a field value from a row by column name
    auto get_field = [&](const std::vector<std::string> &fields,
                         const std::string &col_name) -> const std::string & {
        static const std::string empty;
        auto it = col_index.find(col_name);
        if (it == col_index.end() || it->second >= fields.size()) {
            return empty;
        }
        return fields[it->second];
    };

    std::vector<RawEnchInfo> result;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto &fields = rows[r];
        if (fields.empty()) {
            continue;
        }

        // Required fields
        const std::string &id = get_field(fields, "id");
        if (id.empty()) {
            LOG_WARN("Warning: Skipping CSV row %d with empty id.", r + 1);
            continue;
        }

        int32_t max_level = 0;
        try {
            max_level = std::stoi(get_field(fields, "max_level"));
        } catch (...) {
        }
        if (max_level <= 0) {
            LOG_WARN("Warning: Skipping CSV row %d with invalid max_level.", r + 1);
            continue;
        }

        int32_t multiplier = 0;
        try {
            multiplier = std::stoi(get_field(fields, "multiplier"));
        } catch (...) {
        }
        if (multiplier <= 0) {
            LOG_WARN("Warning: Skipping CSV row %d with invalid multiplier.", r + 1);
            continue;
        }

        // Optional fields
        std::string name = get_field(fields, "name");
        if (name.empty()) {
            name = id;
        }

        std::string platform_str = get_field(fields, "platform");
        MCE platform =
            platform_str.empty() ? MCE::Java : ParserUtils::parse_platform(platform_str);

        int32_t limited_level = max_level;
        {
            auto limited_str = get_field(fields, "limited_level");
            if (!limited_str.empty()) {
                try {
                    limited_level = std::stoi(limited_str);
                } catch (...) {}
            }
        }
        if (limited_level <= 0) {
            limited_level = max_level;
        }

        // Exclusive set — semi-colon separated tokens, resolve #tag refs
        std::unordered_set<std::string> exclusive_set;
        {
            std::string excl_str = get_field(fields, "exclusive_set");
            if (!excl_str.empty()) {
                auto items    = ParserUtils::split_string(excl_str, ';');
                auto resolved = resolve_references(items, tag_resolver);
                exclusive_set = std::move(resolved);
            }
        }

        // Applicable equipment — semi-colon separated tokens, keep as strings
        std::unordered_set<std::string> applicable_equipment;
        {
            std::string eq_str = get_field(fields, "applicable_equipment");
            if (!eq_str.empty()) {
                auto items    = ParserUtils::split_string(eq_str, ';');
                auto resolved = resolve_references(items, tag_resolver);
                applicable_equipment = std::move(resolved);
            }
        }

        result.emplace_back(RawEnchInfo{
            std::move(id),
            std::move(name),
            platform,
            max_level,
            limited_level,
            multiplier,
            false, // is_treasure (not present in CSV format)
            std::move(exclusive_set),
            std::move(applicable_equipment)
        });
    }

    return result;
}

// ============================================================================

std::vector<RawEnchInfo> EnchInfoParser::parse_mc_official(
    const std::filesystem::path &data_pack_dir, TagResolver &tag_resolver
) {
    // Load tags from the data pack directory
    tag_resolver.load_from(data_pack_dir);

    std::vector<RawEnchInfo> result;

    std::filesystem::path data_dir = data_pack_dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return result;
    }

    // Known equipment category IDs (without namespace prefix)
    // Used only for heuristics in MC official format parsing
    std::unordered_set<std::string> known_equipment_ids = {
        "sword",      "helmet",    "chestplate", "leggings",
        "boots",      "bow",       "axe",        "pickaxe",
        "shovel",     "hoe",       "trident",    "shield",
        "crossbow",   "fishing_rod"
    };

    for (const auto &ns_entry : std::filesystem::directory_iterator(data_dir)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        std::string ns = ns_entry.path().filename().string();

        std::filesystem::path ench_dir = ns_entry.path() / "enchantment";
        if (!std::filesystem::is_directory(ench_dir)) {
            continue;
        }

        for (const auto &ench_file : std::filesystem::directory_iterator(ench_dir)) {
            if (!ench_file.is_regular_file()) {
                continue;
            }

            // Check extension is .json (case-insensitive)
            std::string ext = ench_file.path().extension().string();
            for (auto &c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext != ".json") {
                continue;
            }

            std::string filename = ench_file.path().stem().string();

            // Read and parse the JSON file
            std::string content;
            try {
                content = ParserUtils::read_file(ench_file.path());
            } catch (const std::exception &) {
                LOG_WARN("Warning: Could not read %s", ench_file.path().c_str());
                continue;
            }

            Json root;
            try {
                root = Json::parse(content);
            } catch (const std::exception &) {
                LOG_WARN("Warning: Could not parse %s", ench_file.path().c_str());
                continue;
            }

            auto root_var = root.get_value();
            if (!std::holds_alternative<Json::Object>(root_var)) {
                continue;
            }
            const auto &obj = std::get<Json::Object>(root_var);

            // Map MC official fields to EnchInfo
            int32_t multiplier = ParserUtils::get_json_int(obj, "anvil_cost");
            int32_t max_level  = ParserUtils::get_json_int(obj, "max_level");

            if (max_level <= 0 || multiplier <= 0) {
                LOG_WARN("Warning: Skipping %s:%s — invalid max_level or anvil_cost (max_level=%d, anvil_cost=%d)",
                         ns.c_str(), filename.c_str(), max_level, multiplier);
                continue;
            }

            // Limited level defaults to max_level (no field in MC format)
            int32_t limited_level = max_level;

            // Platform defaults to All (MC official is cross-platform)
            MCE platform = MCE::All;

            // Derive display name from filename
            std::string name = filename;
            if (!name.empty()) {
                name[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(name[0]))
                );
            }
            for (auto &c : name) {
                if (c == '_') {
                    c = ' ';
                }
            }

            // Namespaced id
            std::string name_id = ns + ":" + filename;

            // Exclusive set — resolve #tag refs
            auto excl_items    = ParserUtils::get_json_string_array(obj, "exclusive_set");
            auto exclusive_set = resolve_references(excl_items, tag_resolver);

            // Supported items → applicable equipment (keep as strings)
            auto supp_items      = ParserUtils::get_json_string_array(obj, "supported_items");
            auto resolved_supp   = resolve_references(supp_items, tag_resolver);

            std::unordered_set<std::string> applicable_equipment;
            for (const auto &item_id : resolved_supp) {
                // Strip namespace prefix to check against known equipment categories
                std::string stripped = item_id;
                size_t colon_pos     = stripped.find(':');
                if (colon_pos != std::string::npos) {
                    stripped = stripped.substr(colon_pos + 1);
                }

                // Keep the item_id as-is for RegistryResolver to resolve later.
                // Use stripped form if it's a known builtin category name.
                if (known_equipment_ids.count(stripped)) {
                    applicable_equipment.insert(stripped);
                } else {
                    applicable_equipment.insert(item_id);
                }
            }

            result.emplace_back(RawEnchInfo{
                std::move(name_id),
                std::move(name),
                platform,
                max_level,
                limited_level,
                multiplier,
                false, // is_treasure (not in MC official format)
                std::move(exclusive_set),
                std::move(applicable_equipment)
            });
        }
    }

    return result;
}
