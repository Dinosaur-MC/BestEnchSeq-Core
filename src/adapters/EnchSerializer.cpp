#include "adapters/EnchSerializer.h"
#include "parsers/EnchInfoParser.h"   // EnchantmentDataPack
#include "utils/ParserUtils.h"
#include "log/log.hpp"
#include "io/CsvIO.h"
#include "io/json.h"
#include "registries/EquipmentCategoryRegistry.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// Enchantment serialization
// ============================================================================

std::string EnchSerializer::to_json(
    const std::vector<EnchInfo> &infos,
    const EquipmentCategoryRegistry &cat_reg,
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
        obj["is_treasure"] = Json(Json::Bool(info.is_treasure));

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

std::string EnchSerializer::to_csv(
    const std::vector<EnchInfo> &infos,
    const EquipmentCategoryRegistry &cat_reg
) {
    csv::CsvTable table;

    // Header row
    table.push_back({"id", "name", "platform", "max_level", "limited_level",
                     "multiplier", "is_treasure", "exclusive_set", "applicable_equipment"});

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
            info.is_treasure ? "true" : "false",
            excl_set,
            app_eq,
        });
    }

    return csv::format(table);
}

// ============================================================================

void EnchSerializer::export_to_mc_official(
    const std::vector<EnchInfo> &infos,
    const EquipmentCategoryRegistry &cat_reg,
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
                LOG_WARN("Warning: Write error for %s", file_path.c_str());
            }
        } else {
            LOG_WARN("Warning: Could not open %s for writing", file_path.c_str());
        }
    }
}

// ============================================================================
// Equipment serialization
// ============================================================================

std::string EnchSerializer::to_json(
    const std::vector<Equipment> &equipments,
    const EquipmentCategoryRegistry &cat_reg
) {
    Json::Array eq_arr;
    for (const auto &eq : equipments) {
        std::string cat_name = "unknown";
        if (eq.category_id >= 0 && static_cast<size_t>(eq.category_id) < cat_reg.size())
            cat_name = cat_reg.get(eq.category_id).name_id;
        Json::Object obj;
        obj["id"]             = Json(Json::String(eq.name_id));
        obj["name"]           = Json(Json::String(eq.name));
        obj["category"]       = Json(Json::String(cat_name));
        obj["max_durability"] = Json(Json::Number(static_cast<int32_t>(eq.max_durability)));
        eq_arr.push_back(Json(obj));
    }

    Json::Object root;
    root["equipments"] = Json(eq_arr);
    return Json(root).to_string(Json::Pretty);
}

// ============================================================================

std::string EnchSerializer::to_csv(
    const std::vector<Equipment> &equipments,
    const EquipmentCategoryRegistry &cat_reg
) {
    csv::CsvTable table;
    table.push_back({"id", "name", "category", "max_durability"});

    for (const auto &eq : equipments) {
        std::string cat_name2 = "unknown";
        if (eq.category_id >= 0 && static_cast<size_t>(eq.category_id) < cat_reg.size())
            cat_name2 = cat_reg.get(eq.category_id).name_id;
        table.push_back({
            eq.name_id,
            eq.name,
            cat_name2,
            std::to_string(eq.max_durability),
        });
    }

    return csv::format(table);
}
