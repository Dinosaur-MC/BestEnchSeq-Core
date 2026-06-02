#include "parser/EquipmentParser.h"
#include "parser/ParserUtils.h"
#include "io/json.h"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

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

} // anonymous namespace

// ============================================================================

std::vector<EquipmentType> EquipmentParser::parse_native_json(
    const std::filesystem::path &path,
    TagResolver &tag_resolver
) {
    (void)tag_resolver; // Equipment parsing may use tag resolver for categories in future

    // Read and parse the JSON file
    std::string content;
    try {
        content = ParserUtils::read_file(path);
    } catch (const std::exception &e) {
        std::cerr << "Warning: Could not read " << path << ": " << e.what() << std::endl;
        return {};
    }

    Json root;
    try {
        root = Json::parse(content);
    } catch (const std::exception &) {
        std::cerr << "Warning: Could not parse " << path << std::endl;
        return {};
    }

    auto root_var = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) {
        return {};
    }
    const auto &root_obj = std::get<Json::Object>(root_var);

    // Extract equipments array
    auto eq_it = root_obj.find("equipments");
    if (eq_it == root_obj.end()) {
        return {};
    }

    auto eq_val = eq_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(eq_val)) {
        return {};
    }
    const auto &eq_arr = std::get<Json::Array>(eq_val);

    // Parse each equipment entry
    std::vector<EquipmentType> result;
    for (const auto &eq_json : eq_arr) {
        auto elem_val = eq_json.get_value();
        if (!std::holds_alternative<Json::Object>(elem_val)) {
            continue;
        }
        const auto &elem_obj = std::get<Json::Object>(elem_val);

        // Required fields
        std::string id       = get_string_field(elem_obj, "id");
        std::string category = get_string_field(elem_obj, "category");

        if (id.empty() || category.empty()) {
            std::cerr << "Warning: Skipping equipment entry with missing id or category."
                      << std::endl;
            continue;
        }

        // Optional fields
        std::string name = get_string_field(elem_obj, "name");
        if (name.empty()) {
            name = id;
        }

        int32_t max_durability = get_int_field(elem_obj, "max_durability");
        if (max_durability <= 0) {
            max_durability = 0;
        }

        result.emplace_back(EquipmentType{
            std::move(id),
            std::move(name),
            EquipmentCategory(category.c_str()),
            max_durability
        });
    }

    return result;
}

// ============================================================================

std::vector<EquipmentType> EquipmentParser::parse_native_csv(
    const std::filesystem::path &path
) {
    auto rows = ParserUtils::parse_csv(path);
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
    auto req_id       = col_index.find("id");
    auto req_category = col_index.find("category");
    if (req_id == col_index.end() || req_category == col_index.end()) {
        std::cerr << "Warning: CSV file missing required columns (id, category)." << std::endl;
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

    std::vector<EquipmentType> result;
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

        const std::string &category = get_field(fields, "category");
        if (category.empty()) {
            std::cerr << "Warning: Skipping CSV row " << (r + 1)
                      << " with empty category." << std::endl;
            continue;
        }

        // Optional fields
        std::string name = get_field(fields, "name");
        if (name.empty()) {
            name = id;
        }

        int32_t max_durability = 0;
        try {
            max_durability = std::stoi(get_field(fields, "max_durability"));
        } catch (...) {
        }
        if (max_durability <= 0) {
            max_durability = 0;
        }

        result.emplace_back(EquipmentType{
            std::move(id),
            std::move(name),
            EquipmentCategory(category.c_str()),
            max_durability
        });
    }

    return result;
}

// ============================================================================

std::vector<EquipmentType> EquipmentParser::parse_mc_official(
    const std::filesystem::path &data_pack_dir
) {
    // MC does not have a native equipment-type registry.
    // For now, we derive equipment types from item definition files.
    // Scan data/<ns>/items/ for item definition files.
    std::vector<EquipmentType> result;

    std::filesystem::path data_dir = data_pack_dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return result;
    }

    for (const auto &ns_entry : std::filesystem::directory_iterator(data_dir)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        std::string ns = ns_entry.path().filename().string();

        // Check for items/ directory
        std::filesystem::path items_dir = ns_entry.path() / "items";
        if (!std::filesystem::is_directory(items_dir)) {
            continue;
        }

        for (const auto &item_file : std::filesystem::directory_iterator(items_dir)) {
            if (!item_file.is_regular_file()) {
                continue;
            }

            std::string ext = item_file.path().extension().string();
            for (auto &c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext != ".json") {
                continue;
            }

            std::string filename = item_file.path().stem().string();
            std::string item_id  = ns + ":" + filename;

            // Read and parse the JSON file
            std::string content;
            try {
                content = ParserUtils::read_file(item_file.path());
            } catch (const std::exception &) {
                std::cerr << "Warning: Could not read " << item_file.path() << std::endl;
                continue;
            }

            Json root;
            try {
                root = Json::parse(content);
            } catch (const std::exception &) {
                std::cerr << "Warning: Could not parse " << item_file.path() << std::endl;
                continue;
            }

            auto root_var = root.get_value();
            if (!std::holds_alternative<Json::Object>(root_var)) {
                continue;
            }
            const auto &obj = std::get<Json::Object>(root_var);

            // Extract equipment-like fields from the item definition
            // MC item JSON doesn't have an "equipment" concept directly,
            // but we derive durable items from component structure.
            // For now, derive the category from the item id's last segment
            // and set a placeholder max_durability.
            std::string derived_name = filename;
            if (!derived_name.empty()) {
                derived_name[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(derived_name[0]))
                );
            }
            for (auto &c : derived_name) {
                if (c == '_') {
                    c = ' ';
                }
            }

            // Check if this looks like a tool/armor item by looking for
            // durability or component structure
            // (For now, all items found in items/ are treated as equipment)
            int32_t durability = 0;
            // Try to extract max_damage or durability from components
            auto components_it = obj.find("components");
            if (components_it != obj.end()) {
                auto comp_val = components_it->second.get_value();
                if (std::holds_alternative<Json::Object>(comp_val)) {
                    const auto &comp_obj = std::get<Json::Object>(comp_val);
                    durability = get_int_field(comp_obj, "max_damage");
                    if (durability <= 0) {
                        durability = get_int_field(comp_obj, "durability");
                    }
                }
            }

            // Derive category from the item type
            std::string category_str = filename;
            // Use common tool/armor patterns to determine category
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
                {"bow", "bow"},
                {"crossbow", "crossbow"},
                {"trident", "trident"},
                {"shield", "shield"},
                {"fishing_rod", "fishing_rod"},
                {"elytra", "elytra"},
                {"_horse_armor", "horse_armor"},
            };

            bool matched = false;
            for (const auto &[suffix, cat] : suffix_to_category) {
                if (category_str == suffix ||
                    (category_str.size() > suffix.size() &&
                     category_str.substr(category_str.size() - suffix.size()) == suffix)) {
                    category_str = cat;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                category_str = filename;
            }

            result.emplace_back(EquipmentType{
                std::move(item_id),
                std::move(derived_name),
                EquipmentCategory(category_str.c_str()),
                durability
            });
        }
    }

    return result;
}

// ============================================================================

std::vector<EquipmentType> EquipmentParser::parse(
    const std::filesystem::path &path,
    TagResolver &tag_resolver
) {
    auto format = ParserUtils::detect_format(path);
    switch (format) {
    case ParserUtils::DataFormat::NativeJSON:
        return parse_native_json(path, tag_resolver);
    case ParserUtils::DataFormat::NativeCSV:
        return parse_native_csv(path);
    case ParserUtils::DataFormat::MCOfficial:
        return parse_mc_official(path);
    default:
        throw std::runtime_error("Unknown format: " + path.string());
    }
}
