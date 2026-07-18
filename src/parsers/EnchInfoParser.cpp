#include "parsers/EnchInfoParser.h"
#include "parsers/ParserUtilsDomain.hpp"
#include "utils/ParserUtils.hpp"
#include "log/log.hpp"
#include "io/CsvIO.h"
#include "io/json.h"

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Inline tag processing (private helpers)
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

// ---------------------------------------------------------------------------
// Build a qualified Id from a bare or namespaced string
// ---------------------------------------------------------------------------
Id make_id(const std::string &id_str, const std::string &default_ns = "minecraft") {
    Id id;
    if (id_str.find(':') != std::string::npos) {
        auto [ns, path] = ParserUtils::split_namespace(id_str);
        id.ns = ns;
        id.path = path;
    } else {
        id.ns = default_ns;
        id.path = id_str;
    }
    return id;
}

// ---------------------------------------------------------------------------
// Parse a single enchantment entry from a JSON object (native format)
// ---------------------------------------------------------------------------
RawEnchantment parse_ench_entry(const Json::Object &elem_obj, TagResolver &tag_resolver) {
    // Required fields
    std::string id_str      = ParserUtils::get_json_string(elem_obj, "id");
    int32_t max_level       = ParserUtils::get_json_int(elem_obj, "max_level");
    int32_t multiplier      = ParserUtils::get_json_int(elem_obj, "multiplier");

    // Optional fields
    std::string display_name = ParserUtils::get_json_string(elem_obj, "name");
    if (display_name.empty()) {
        display_name = id_str;
    }

    int32_t limited_level = ParserUtils::get_json_int(elem_obj, "limited_level");
    if (limited_level <= 0) {
        limited_level = 0; // 0 = treasure
    }

    // Exclusive set — resolve #tag references
    auto exclusive_set_items = ParserUtils::get_json_string_array(elem_obj, "exclusive_set");
    auto exclusive_set       = resolve_references(exclusive_set_items, tag_resolver);

    // Applicable items — resolve #tag references
    auto app_items       = ParserUtils::get_json_string_array(elem_obj, "applicable_equipment");
    auto applicable_items = resolve_references(app_items, tag_resolver);

    RawEnchantment ench;
    ench.id               = make_id(id_str);
    ench.display_name     = std::move(display_name);
    ench.multiplier       = multiplier;
    ench.max_level        = max_level;
    ench.limited_level    = limited_level;
    ench.exclusive_set    = std::move(exclusive_set);
    ench.applicable_items = std::move(applicable_items);
    return ench;
}

// ---------------------------------------------------------------------------
// Parse equipment array from a native JSON root object
// ---------------------------------------------------------------------------
std::vector<RawEquipment> parse_equipments_json(const Json::Object &root_obj) {
    auto eq_it = root_obj.find("equipments");
    if (eq_it == root_obj.end()) {
        return {};
    }

    auto eq_val = eq_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(eq_val)) {
        return {};
    }
    const auto &eq_arr = std::get<Json::Array>(eq_val);

    std::vector<RawEquipment> result;
    for (const auto &eq_json : eq_arr) {
        auto elem_val = eq_json.get_value();
        if (!std::holds_alternative<Json::Object>(elem_val)) {
            continue;
        }
        const auto &elem_obj = std::get<Json::Object>(elem_val);

        std::string id_str   = ParserUtils::get_json_string(elem_obj, "id");
        std::string category = ParserUtils::get_json_string(elem_obj, "category");

        if (id_str.empty() || category.empty()) {
            LOG_WARN("Warning: Skipping equipment entry with missing id or category.");
            continue;
        }

        std::string name = ParserUtils::get_json_string(elem_obj, "name");
        if (name.empty()) {
            name = id_str;
        }

        int32_t max_durability = ParserUtils::get_json_int(elem_obj, "max_durability");
        if (max_durability <= 0) {
            max_durability = 0;
        }

        RawEquipment eq;
        eq.id             = make_id(id_str);
        eq.display_name   = std::move(name);
        eq.category       = std::move(category);
        eq.max_durability = max_durability;
        result.push_back(std::move(eq));
    }

    return result;
}

// ---------------------------------------------------------------------------
// Known builtin item → durability table for MC official format
// ---------------------------------------------------------------------------
int32_t get_builtin_durability(const std::string &item_id) {
    // Strip namespace for lookup
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos) {
        key = key.substr(colon + 1);
    }

    static const std::unordered_map<std::string, int32_t> durability_map = {
        // Swords
        {"wooden_sword", 59},     {"stone_sword", 131},
        {"iron_sword", 250},      {"golden_sword", 32},
        {"diamond_sword", 1561},  {"netherite_sword", 2031},
        // Pickaxes
        {"wooden_pickaxe", 59},   {"stone_pickaxe", 131},
        {"iron_pickaxe", 250},    {"golden_pickaxe", 32},
        {"diamond_pickaxe", 1561},{"netherite_pickaxe", 2031},
        // Axes
        {"wooden_axe", 59},       {"stone_axe", 131},
        {"iron_axe", 250},        {"golden_axe", 32},
        {"diamond_axe", 1561},    {"netherite_axe", 2031},
        // Shovels
        {"wooden_shovel", 59},    {"stone_shovel", 131},
        {"iron_shovel", 250},     {"golden_shovel", 32},
        {"diamond_shovel", 1561}, {"netherite_shovel", 2031},
        // Hoes
        {"wooden_hoe", 59},       {"stone_hoe", 131},
        {"iron_hoe", 250},        {"golden_hoe", 32},
        {"diamond_hoe", 1561},    {"netherite_hoe", 2031},
        // Helmets
        {"leather_helmet", 55},   {"chainmail_helmet", 165},
        {"iron_helmet", 165},     {"golden_helmet", 77},
        {"diamond_helmet", 363},  {"netherite_helmet", 407},
        // Chestplates
        {"leather_chestplate", 80},   {"chainmail_chestplate", 240},
        {"iron_chestplate", 240},     {"golden_chestplate", 112},
        {"diamond_chestplate", 528},  {"netherite_chestplate", 592},
        // Leggings
        {"leather_leggings", 75},     {"chainmail_leggings", 225},
        {"iron_leggings", 225},       {"golden_leggings", 105},
        {"diamond_leggings", 495},    {"netherite_leggings", 555},
        // Boots
        {"leather_boots", 65},        {"chainmail_boots", 195},
        {"iron_boots", 195},          {"golden_boots", 91},
        {"diamond_boots", 429},       {"netherite_boots", 481},
        // Other tools
        {"bow", 384},                 {"crossbow", 465},
        {"trident", 250},             {"shield", 336},
        {"fishing_rod", 64},          {"elytra", 432},
        {"shears", 238},              {"brush", 64},
        // Turtle shell
        {"turtle_helmet", 275},
        // Mace
        {"mace", 250},
    };

    auto it = durability_map.find(key);
    if (it != durability_map.end()) {
        return it->second;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Derive category from item ID suffix
// ---------------------------------------------------------------------------
std::string derive_category(const std::string &item_id) {
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos) {
        key = key.substr(colon + 1);
    }

    static const std::unordered_map<std::string, std::string> suffix_to_category = {
        {"_sword", "sword"},     {"_pickaxe", "pickaxe"},
        {"_axe", "axe"},         {"_shovel", "shovel"},
        {"_hoe", "hoe"},         {"_helmet", "helmet"},
        {"_chestplate", "chestplate"}, {"_leggings", "leggings"},
        {"_boots", "boots"},     {"_horse_armor", "horse_armor"},
        {"bow", "bow"},          {"crossbow", "crossbow"},
        {"trident", "trident"},  {"shield", "shield"},
        {"fishing_rod", "fishing_rod"}, {"elytra", "elytra"},
        {"_skull", "skull"},     {"_head", "head"},
        {"mace", "mace"},        {"brush", "brush"},
    };

    for (const auto &[suffix, cat] : suffix_to_category) {
        if (key == suffix ||
            (key.size() > suffix.size() &&
             key.substr(key.size() - suffix.size()) == suffix)) {
            return cat;
        }
    }

    return key;
}

// ---------------------------------------------------------------------------
// Derive display name from an item ID string
// ---------------------------------------------------------------------------
std::string derive_display_name(const std::string &item_id) {
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos) {
        key = key.substr(colon + 1);
    }

    if (!key.empty()) {
        key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    }
    for (auto &c : key) {
        if (c == '_') {
            c = ' ';
        }
    }
    return key;
}

// ---------------------------------------------------------------------------
// Compute limited_level from min_cost formula and item enchantability
// ---------------------------------------------------------------------------
int32_t compute_limited_level(
    int32_t max_level,
    int32_t min_cost_base,
    int32_t min_cost_per_level,
    const std::unordered_set<std::string>& applicable_items)
{
    static const std::unordered_map<std::string, int32_t> ENCHANTABILITY = {
        // Tools
        {"wooden_sword", 15}, {"wooden_pickaxe", 15}, {"wooden_axe", 15},
        {"wooden_shovel", 15}, {"wooden_hoe", 15},
        {"stone_sword", 5}, {"stone_pickaxe", 5}, {"stone_axe", 5},
        {"stone_shovel", 5}, {"stone_hoe", 5},
        {"iron_sword", 14}, {"iron_pickaxe", 14}, {"iron_axe", 14},
        {"iron_shovel", 14}, {"iron_hoe", 14},
        {"golden_sword", 22}, {"golden_pickaxe", 22}, {"golden_axe", 22},
        {"golden_shovel", 22}, {"golden_hoe", 22},
        {"diamond_sword", 10}, {"diamond_pickaxe", 10}, {"diamond_axe", 10},
        {"diamond_shovel", 10}, {"diamond_hoe", 10},
        {"netherite_sword", 15}, {"netherite_pickaxe", 15}, {"netherite_axe", 15},
        {"netherite_shovel", 15}, {"netherite_hoe", 15},
        // Armor
        {"leather_helmet", 15}, {"leather_chestplate", 15},
        {"leather_leggings", 15}, {"leather_boots", 15},
        {"chainmail_helmet", 12}, {"chainmail_chestplate", 12},
        {"chainmail_leggings", 12}, {"chainmail_boots", 12},
        {"iron_helmet", 9}, {"iron_chestplate", 9},
        {"iron_leggings", 9}, {"iron_boots", 9},
        {"golden_helmet", 25}, {"golden_chestplate", 25},
        {"golden_leggings", 25}, {"golden_boots", 25},
        {"diamond_helmet", 10}, {"diamond_chestplate", 10},
        {"diamond_leggings", 10}, {"diamond_boots", 10},
        {"netherite_helmet", 15}, {"netherite_chestplate", 15},
        {"netherite_leggings", 15}, {"netherite_boots", 15},
        {"turtle_helmet", 9},
        // Special
        {"bow", 1}, {"crossbow", 1}, {"trident", 1},
        {"fishing_rod", 1}, {"book", 1}, {"mace", 15},
        // Wolf armor
        {"wolf_armor", 0},  // not enchantable via table
    };

    auto max_power = [](int32_t enchantability) -> int32_t {
        if (enchantability <= 0) return 0;
        double base = 30.0;
        double added = 1.0 + 2.0 * (static_cast<double>(enchantability) / 4.0);
        return static_cast<int32_t>(std::round((base + added) * 1.15));
    };

    int32_t best = 0;
    for (const auto& item : applicable_items) {
        // Strip namespace prefix to look up bare name
        std::string bare = item;
        auto colon = bare.find(':');
        if (colon != std::string::npos)
            bare = bare.substr(colon + 1);

        auto it = ENCHANTABILITY.find(bare);
        if (it == ENCHANTABILITY.end() || it->second <= 0)
            continue;

        int32_t power = max_power(it->second);
        // Closed-form O(1): find highest level where min_cost(lvl) <= power
        // min_cost(lvl) = min_cost_base + min_cost_per_level * (lvl - 1)
        // => lvl <= (power - min_cost_base) / min_cost_per_level + 1
        if (power >= min_cost_base) {
            int32_t max_lvl = (power - min_cost_base) / min_cost_per_level + 1;
            if (max_lvl > max_level) max_lvl = max_level;
            if (max_lvl > best) best = max_lvl;
        }
    }
    return std::max<int32_t>(1, best);
}

// ---------------------------------------------------------------------------
// Scan MC official data pack for item tags and derive equipment
// ---------------------------------------------------------------------------
std::vector<RawEquipment> derive_equipment_from_tags(const std::filesystem::path &data_dir) {
    std::unordered_set<std::string> item_ids;
    std::unordered_set<std::string> seen_ids;

    // Scan data/<ns>/tags/item/ for all item tag files
    for (const auto &ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) continue;

        std::filesystem::path tags_item_dir = ns_entry.path() / "tags" / "item";
        if (!std::filesystem::is_directory(tags_item_dir)) continue;

        // Recursively scan all .json tag files under tags/item/
        try {
            for (const auto &file_entry :
                 std::filesystem::recursive_directory_iterator(
                     tags_item_dir,
                     std::filesystem::directory_options::skip_permission_denied)) {
                if (!file_entry.is_regular_file()) continue;
                if (file_entry.path().extension() != ".json") continue;

                // Parse and extract values
                try {
                    std::string content = ParserUtils::read_file(file_entry.path());
                    Json json = Json::parse(content);
                    auto root_var = json.get_value();
                    if (!std::holds_alternative<Json::Object>(root_var)) continue;
                    const auto &root_obj = std::get<Json::Object>(root_var);

                    auto values_it = root_obj.find("values");
                    if (values_it == root_obj.end()) continue;
                    auto values_var = values_it->second.get_value();
                    if (!std::holds_alternative<Json::Array>(values_var)) continue;
                    const auto &values_arr = std::get<Json::Array>(values_var);

                    for (const auto &elem : values_arr) {
                        auto elem_var = elem.get_value();
                        if (std::holds_alternative<Json::String>(elem_var)) {
                            std::string val = std::get<Json::String>(elem_var);
                            // Only collect concrete IDs (not #tag refs)
                            if (!val.empty() && val[0] != '#') {
                                if (seen_ids.insert(val).second) {
                                    item_ids.insert(val);
                                }
                            }
                        }
                    }
                } catch (const std::exception &) {
                    continue;
                }
            }
        } catch (const std::filesystem::filesystem_error &) {
            continue;
        }
    }

    // Build RawEquipment from collected items
    std::vector<RawEquipment> result;
    for (const auto &item_id : item_ids) {
        int32_t durability = get_builtin_durability(item_id);
        std::string category = derive_category(item_id);
        // Skip items that don't look like equipment (no durability + generic category)
        if (durability <= 0 && category == item_id) {
            // Keep items with a namespace — they might be custom items
            if (item_id.find(':') != std::string::npos) {
                auto [ns, path] = ParserUtils::split_namespace(item_id);
                // Skip if it doesn't match any known pattern
                (void)ns;
            } else {
                continue;
            }
        }

        RawEquipment eq;
        eq.id = make_id(item_id);
        eq.display_name = derive_display_name(item_id);
        eq.category = category;
        eq.max_durability = durability;
        result.push_back(std::move(eq));
    }

    return result;
}

} // anonymous namespace

// ============================================================================

std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
EnchInfoParser::parse_native_json(
    const std::filesystem::path &path,
    EnchantmentDataPack *metadata
) {
    return parse_native_json_str(ParserUtils::read_file(path), metadata);
}

// ============================================================================

std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
EnchInfoParser::parse_native_json_str(
    const std::string &content,
    EnchantmentDataPack *metadata
) {
    TagResolver tag_resolver;
    Json root = Json::parse(content);

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
    std::vector<RawEnchantment> enchantments;
    if (ench_it != root_obj.end()) {
        auto ench_val = ench_it->second.get_value();
        if (std::holds_alternative<Json::Array>(ench_val)) {
            const auto &ench_arr = std::get<Json::Array>(ench_val);

            for (const auto &ench_json : ench_arr) {
                auto elem_val = ench_json.get_value();
                if (!std::holds_alternative<Json::Object>(elem_val)) {
                    continue;
                }
                const auto &elem_obj = std::get<Json::Object>(elem_val);

                // Required fields
                std::string id     = ParserUtils::get_json_string(elem_obj, "id");
                int32_t max_level  = ParserUtils::get_json_int(elem_obj, "max_level");
                int32_t multiplier = ParserUtils::get_json_int(elem_obj, "multiplier");

                if (id.empty() || max_level <= 0 || multiplier <= 0) {
                    LOG_WARN("Warning: Skipping enchantment entry with missing or invalid required fields (id='%s', max_level=%d, multiplier=%d)",
                             id.c_str(), max_level, multiplier);
                    continue;
                }

                enchantments.push_back(parse_ench_entry(elem_obj, tag_resolver));
            }
        }
    }

    // --- Extract equipment array -------------------------------------------
    auto equipment = parse_equipments_json(root_obj);

    return {std::move(enchantments), std::move(equipment)};
}

// ============================================================================

std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
EnchInfoParser::parse(
    const std::filesystem::path &path,
    EnchantmentDataPack *metadata
) {
    // Auto-detect format
    auto format = ParserUtils::detect_format(path);
    switch (format) {
    case ParserUtils::DataFormat::NativeJSON:
        return parse_native_json(path, metadata);
    case ParserUtils::DataFormat::NativeCSV:
        return parse_native_csv(path);
    case ParserUtils::DataFormat::MCOfficial:
        return parse_mc_official(path);
    default:
        throw std::runtime_error("Unknown format: " + path.string());
    }
}

// ============================================================================

std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
EnchInfoParser::parse_native_csv(const std::filesystem::path &path) {
    TagResolver tag_resolver;
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

    std::vector<RawEnchantment> enchantments;
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
        } catch (const std::exception &) {
        }
        if (max_level <= 0) {
            LOG_WARN("Warning: Skipping CSV row %d with invalid max_level.", r + 1);
            continue;
        }

        int32_t multiplier = 0;
        try {
            multiplier = std::stoi(get_field(fields, "multiplier"));
        } catch (const std::exception &) {
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

        int32_t limited_level = max_level;
        {
            auto limited_str = get_field(fields, "limited_level");
            if (!limited_str.empty()) {
                try {
                    limited_level = std::stoi(limited_str);
                } catch (const std::exception &) {}
            }
        }
        if (limited_level <= 0) {
            limited_level = 0; // 0 = treasure
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

        // Applicable items — semi-colon separated tokens
        std::unordered_set<std::string> applicable_items;
        {
            std::string eq_str = get_field(fields, "applicable_equipment");
            if (!eq_str.empty()) {
                auto items    = ParserUtils::split_string(eq_str, ';');
                auto resolved = resolve_references(items, tag_resolver);
                applicable_items = std::move(resolved);
            }
        }

        RawEnchantment ench;
        ench.id               = make_id(id);
        ench.display_name     = std::move(name);
        ench.multiplier       = multiplier;
        ench.max_level        = max_level;
        ench.limited_level    = limited_level;
        ench.exclusive_set    = std::move(exclusive_set);
        ench.applicable_items = std::move(applicable_items);
        enchantments.push_back(std::move(ench));
    }

    // CSV has no equipment data
    return {std::move(enchantments), std::vector<RawEquipment>{}};
}

// ============================================================================

std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
EnchInfoParser::parse_mc_official(const std::filesystem::path &dir) {
    // Create a local TagResolver and load tags from the data pack
    TagResolver tag_resolver;
    tag_resolver.load_from(dir);

    std::vector<RawEnchantment> enchantments;

    std::filesystem::path data_dir = dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return {};
    }

    for (const auto &ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        std::string ns = ns_entry.path().filename().string();

        std::filesystem::path ench_dir = ns_entry.path() / "enchantment";
        if (!std::filesystem::is_directory(ench_dir)) {
            continue;
        }

        for (const auto &ench_file : std::filesystem::directory_iterator(
                 ench_dir, std::filesystem::directory_options::skip_permission_denied)) {
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

            // Map MC official fields
            int32_t multiplier = ParserUtils::get_json_int(obj, "anvil_cost");
            int32_t max_level  = ParserUtils::get_json_int(obj, "max_level");

            if (max_level <= 0 || multiplier <= 0) {
                LOG_WARN("Warning: Skipping %s:%s — invalid max_level or anvil_cost (max_level=%d, anvil_cost=%d)",
                         ns.c_str(), filename.c_str(), max_level, multiplier);
                continue;
            }

            // Derive display name from filename
            std::string display_name = derive_display_name(filename);

            // Exclusive set — resolve #tag refs
            auto excl_items    = ParserUtils::get_json_string_array(obj, "exclusive_set");
            auto exclusive_set = resolve_references(excl_items, tag_resolver);

            // Supported items — resolve #tag refs
            auto supp_items    = ParserUtils::get_json_string_array(obj, "supported_items");
            auto applicable_items = resolve_references(supp_items, tag_resolver);

            // Compute limited_level from cost formula (not a direct field in MC official format)
            int32_t limited_level = max_level;
            auto min_cost_it = obj.find("min_cost");
            if (min_cost_it != obj.end()) {
                auto mc = min_cost_it->second.get_value();
                if (auto* mc_obj = std::get_if<Json::Object>(&mc)) {
                    int32_t min_base = ParserUtils::get_json_int(*mc_obj, "base");
                    int32_t min_per_level = ParserUtils::get_json_int(*mc_obj, "per_level_above_first");
                    if (min_base > 0 && min_per_level >= 0) {
                        limited_level = compute_limited_level(
                            max_level, min_base, min_per_level, applicable_items);
                    }
                }
            }

            RawEnchantment ench;
            ench.id               = make_id(filename, ns);
            ench.display_name     = std::move(display_name);
            ench.multiplier       = multiplier;
            ench.max_level        = max_level;
            ench.limited_level    = limited_level;
            ench.exclusive_set    = std::move(exclusive_set);
            ench.applicable_items = std::move(applicable_items);
            enchantments.push_back(std::move(ench));
        }
    }

    // Derive equipment from item tag files
    auto equipment = derive_equipment_from_tags(data_dir);

    return {std::move(enchantments), std::move(equipment)};
}
