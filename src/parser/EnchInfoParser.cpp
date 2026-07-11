#include "parser/EnchInfoParser.h"
#include "parser/ParserUtils.h"
#include "io/CsvIO.h"
#include "io/json.h"
#include "registries/EquipmentCategoryRegistry.h"

#include <cctype>
#include <fstream>
#include <iostream>
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
    const EquipmentCategoryRegistry &cat_reg,
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
    std::vector<EnchInfo> result;
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
            std::cerr << "Warning: Skipping enchantment entry with missing or invalid "
                         "required fields (id='"
                      << id << "', max_level=" << max_level
                      << ", multiplier=" << multiplier << ")" << std::endl;
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

        // Exclusive set — resolve #tag references
        auto exclusive_set_items = ParserUtils::get_json_string_array(elem_obj, "exclusive_set");
        auto exclusive_set       = resolve_references(exclusive_set_items, tag_resolver);

        // Applicable equipment — resolve #tag references
        auto equipment_items = ParserUtils::get_json_string_array(elem_obj, "applicable_equipment");
        std::unordered_set<int32_t> applicable_category_ids;
        auto resolved_equipment = resolve_references(equipment_items, tag_resolver);
        for (const auto &eq : resolved_equipment) {
            int32_t cid = cat_reg.get_id(eq);
            if (cid >= 0)
                applicable_category_ids.insert(cid);
        }

        result.emplace_back(
            std::move(id),
            std::move(name),
            platform,
            max_level,
            limited_level,
            multiplier,
            std::move(exclusive_set),
            std::move(applicable_category_ids)
        );
    }

    return result;
}

// ============================================================================

std::vector<EnchInfo> EnchInfoParser::parse(
    const std::filesystem::path &path, TagResolver &tag_resolver,
    const EquipmentCategoryRegistry &cat_reg
) {
    // Auto-detect format
    auto format = ParserUtils::detect_format(path);
    switch (format) {
    case ParserUtils::DataFormat::NativeJSON:
        return parse_native_json(path, tag_resolver, cat_reg);
    case ParserUtils::DataFormat::NativeCSV:
        return parse_native_csv(path, tag_resolver, cat_reg);
    case ParserUtils::DataFormat::MCOfficial:
        return parse_mc_official(path, tag_resolver, cat_reg);
    default:
        throw std::runtime_error("Unknown format: " + path.string());
    }
}

// ============================================================================

std::vector<EnchInfo> EnchInfoParser::parse_native_csv(
    const std::filesystem::path &path, TagResolver &tag_resolver,
    const EquipmentCategoryRegistry &cat_reg
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
        std::cerr << "Warning: CSV file missing required columns (id, max_level, multiplier)."
                  << std::endl;
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

    std::vector<EnchInfo> result;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto &fields = rows[r];
        if (fields.empty()) {
            continue;
        }

        // Required fields
        const std::string &id = get_field(fields, "id");
        if (id.empty()) {
            std::cerr << "Warning: Skipping CSV row " << (r + 1)
                      << " with empty id." << std::endl;
            continue;
        }

        int32_t max_level = 0;
        try {
            max_level = std::stoi(get_field(fields, "max_level"));
        } catch (...) {
        }
        if (max_level <= 0) {
            std::cerr << "Warning: Skipping CSV row " << (r + 1)
                      << " with invalid max_level." << std::endl;
            continue;
        }

        int32_t multiplier = 0;
        try {
            multiplier = std::stoi(get_field(fields, "multiplier"));
        } catch (...) {
        }
        if (multiplier <= 0) {
            std::cerr << "Warning: Skipping CSV row " << (r + 1)
                      << " with invalid multiplier." << std::endl;
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

        // Applicable equipment — semi-colon separated tokens, resolve #tag refs
        std::unordered_set<int32_t> applicable_category_ids;
        {
            std::string eq_str = get_field(fields, "applicable_equipment");
            if (!eq_str.empty()) {
                auto items    = ParserUtils::split_string(eq_str, ';');
                auto resolved = resolve_references(items, tag_resolver);
                for (const auto &eq : resolved) {
                    int32_t cid = cat_reg.get_id(eq);
                    if (cid >= 0)
                        applicable_category_ids.insert(cid);
                }
            }
        }

        result.emplace_back(
            std::move(id),
            std::move(name),
            platform,
            max_level,
            limited_level,
            multiplier,
            std::move(exclusive_set),
            std::move(applicable_category_ids)
        );
    }

    return result;
}

// ============================================================================

std::vector<EnchInfo> EnchInfoParser::parse_mc_official(
    const std::filesystem::path &data_pack_dir, TagResolver &tag_resolver,
    const EquipmentCategoryRegistry &cat_reg
) {
    // Load tags from the data pack directory
    tag_resolver.load_from(data_pack_dir);

    std::vector<EnchInfo> result;

    std::filesystem::path data_dir = data_pack_dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return result;
    }

    // Known equipment category IDs (without namespace prefix)
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
                std::cerr << "Warning: Could not read " << ench_file.path() << std::endl;
                continue;
            }

            Json root;
            try {
                root = Json::parse(content);
            } catch (const std::exception &) {
                std::cerr << "Warning: Could not parse " << ench_file.path() << std::endl;
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
                std::cerr << "Warning: Skipping " << ns << ":" << filename
                          << " — invalid max_level or anvil_cost (max_level="
                          << max_level << ", anvil_cost=" << multiplier << ")"
                          << std::endl;
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

            // Supported items → applicable equipment
            auto supp_items      = ParserUtils::get_json_string_array(obj, "supported_items");
            auto resolved_supp   = resolve_references(supp_items, tag_resolver);

            std::unordered_set<int32_t> applicable_category_ids;
            for (const auto &item_id : resolved_supp) {
                // Strip namespace prefix to check against known equipment categories
                std::string stripped = item_id;
                size_t colon_pos     = stripped.find(':');
                if (colon_pos != std::string::npos) {
                    stripped = stripped.substr(colon_pos + 1);
                }

                int32_t cat_id;
                if (known_equipment_ids.count(stripped)) {
                    cat_id = cat_reg.get_id(stripped);
                } else {
                    cat_id = cat_reg.get_id(item_id);
                }
                if (cat_id >= 0)
                    applicable_category_ids.insert(cat_id);
            }

            result.emplace_back(
                std::move(name_id),
                std::move(name),
                platform,
                max_level,
                limited_level,
                multiplier,
                std::move(exclusive_set),
                std::move(applicable_category_ids)
            );
        }
    }

    return result;
}

// ============================================================================

std::string EnchInfoParser::to_json(
    const std::vector<EnchInfo> &infos, const EquipmentCategoryRegistry &cat_reg,
    const EnchantmentDataPack *metadata
) {
    Json::Object root;

    // Add metadata if provided
    if (metadata) {
        root["name"]        = Json(Json::String(metadata->name));
        root["description"] = Json(Json::String(metadata->description));
        root["author"]      = Json(Json::String(metadata->author));
        root["version"]     = Json(Json::String(metadata->version));
    }

    // Build enchantments array
    Json::Array enchants;
    for (const auto &info : infos) {
        Json::Object obj;
        obj["id"]       = Json(Json::String(info.name_id));
        obj["name"]     = Json(Json::String(info.name));
        obj["platform"] = Json(Json::String(ParserUtils::platform_to_string(info.supported_platform)));
        obj["max_level"] = Json(Json::Number(static_cast<int32_t>(info.max_level)));
        obj["limited_level"] = Json(Json::Number(static_cast<int32_t>(info.limited_level)));
        obj["multiplier"] = Json(Json::Number(static_cast<int32_t>(info.multiplier)));

        // exclusive_set array
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            excl.push_back(Json(Json::String(e)));
        }
        obj["exclusive_set"] = Json(excl);

        // applicable_equipment array
        Json::Array eq;
        for (const auto &cat_id : info.applicable_category_ids) {
            std::string cat_name = "unknown";
            if (cat_id >= 0 && static_cast<size_t>(cat_id) < cat_reg.size())
                cat_name = cat_reg.get(cat_id).name_id;
            eq.push_back(Json(Json::String(cat_name)));
        }
        obj["applicable_equipment"] = Json(eq);

        enchants.push_back(Json(obj));
    }
    root["enchantments"] = Json(enchants);

    return Json(root).to_string(Json::Pretty);
}

// ============================================================================

std::string EnchInfoParser::to_csv(
    const std::vector<EnchInfo> &infos, const EquipmentCategoryRegistry &cat_reg
) {
    csv::CsvTable table;

    // Header row
    table.push_back({"id", "name", "platform", "max_level", "limited_level",
                     "multiplier", "exclusive_set", "applicable_equipment"});

    for (const auto &info : infos) {
        // exclusive_set: join with ;
        std::string excl_set;
        bool first = true;
        for (const auto &e : info.exclusive_set) {
            if (!first) excl_set += ";";
            first = false;
            excl_set += e;
        }

        // applicable_equipment: join with ;
        std::string app_eq;
        first = true;
        for (const auto &cat_id : info.applicable_category_ids) {
            if (!first) app_eq += ";";
            first = false;
            std::string cat_name = "unknown";
            if (cat_id >= 0 && static_cast<size_t>(cat_id) < cat_reg.size())
                cat_name = cat_reg.get(cat_id).name_id;
            app_eq += cat_name;
        }

        table.push_back({
            info.name_id,
            info.name,
            ParserUtils::platform_to_string(info.supported_platform),
            std::to_string(info.max_level),
            std::to_string(info.limited_level),
            std::to_string(info.multiplier),
            excl_set,
            app_eq,
        });
    }

    return csv::format(table);
}

// ============================================================================

void EnchInfoParser::export_to_mc_official(
    const std::vector<EnchInfo> &infos, const EquipmentCategoryRegistry &cat_reg,
    const std::filesystem::path &output_dir
) {
    for (const auto &info : infos) {
        // Split name_id into namespace and id
        auto [ns, id] = ParserUtils::split_namespace(info.name_id);

        // Construct output path: <output_dir>/data/<ns>/enchantment/<id>.json
        std::filesystem::path ench_dir = output_dir / "data" / ns / "enchantment";
        std::filesystem::create_directories(ench_dir);

        Json::Object obj;
        obj["anvil_cost"] = Json(Json::Number(static_cast<int32_t>(info.multiplier)));
        obj["max_level"]  = Json(Json::Number(static_cast<int32_t>(info.max_level)));

        // exclusive_set as namespaced IDs
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            std::string qualified = ParserUtils::qualify_id(e);
            excl.push_back(Json(Json::String(qualified)));
        }
        obj["exclusive_set"] = Json(excl);

        // supported_items — convert category IDs back to item IDs
        Json::Array supp;
        for (const auto &cat_id : info.applicable_category_ids) {
            std::string cat_str = "unknown";
            if (cat_id >= 0 && static_cast<size_t>(cat_id) < cat_reg.size())
                cat_str = cat_reg.get(cat_id).name_id;
            // Avoid double-namespacing: cat may already contain "mod:item"
            if (cat_str.find(':') != std::string::npos) {
                supp.push_back(Json(Json::String(cat_str)));
            } else {
                supp.push_back(Json(Json::String("minecraft:" + cat_str)));
            }
        }
        obj["supported_items"] = Json(supp);

        // Write the file
        std::string json_str = Json(obj).to_string(Json::Pretty);
        std::filesystem::path file_path = ench_dir / (id + ".json");
        std::ofstream f(file_path);
        if (f.is_open()) {
            f << json_str;
            if (!f.good()) {
                std::cerr << "Warning: Write error for " << file_path << std::endl;
            }
        } else {
            std::cerr << "Warning: Could not open " << file_path << " for writing" << std::endl;
        }
    }
}
