#include "EnchSerializer.h"
#include "common/io/CsvIO.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "domain/business/business.h"
#include "domain/business/types/EnchantmentDataPack.h"
#include "domain/interface/components/ParserUtilsDomain.hpp"
#include "domain/business/types/EnchInfo.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// Enchantment serialization
// ============================================================================

std::string EnchSerializer::to_json(
    const std::vector<EnchInfo> &infos, const EquipmentTagRegistry &cat_reg,
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
        obj["id"]            = Json(Json::String(info.id.str()));
        obj["name"]          = Json(Json::String(info.name));
        obj["platform"]      = Json(Json::String(ParserUtils::platform_to_string(info.supported_platform)));
        obj["max_level"]     = Json(Json::Number(static_cast<int32_t>(info.max_level)));
        obj["limited_level"] = Json(Json::Number(static_cast<int32_t>(info.limited_level)));
        obj["multiplier"]    = Json(Json::Number(static_cast<int32_t>(info.multiplier)));
        obj["is_treasure"]   = Json(Json::Bool(info.is_treasure));

        // exclusive_set array
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            excl.push_back(Json(Json::String(e.str())));
        }
        obj["exclusive_set"] = Json(excl);

        // applicable_equipment array
        Json::Array eq;
        for (const auto &cat_nsid : info.applicable_equipments) {
            auto cat_it = cat_reg.find(cat_nsid);
            std::string cat_name = cat_it != cat_reg.end() ? cat_it->name : "unknown";
            eq.push_back(Json(Json::String(cat_name)));
        }
        obj["applicable_equipment"] = Json(eq);

        enchants.push_back(Json(obj));
    }
    root["enchantments"] = Json(enchants);

    return Json(root).to_string(Json::Pretty);
}

// ============================================================================

std::string
EnchSerializer::to_csv(const std::vector<EnchInfo> &infos, const EquipmentTagRegistry &cat_reg) {
    csv::CsvTable table;

    // Header row
    table.push_back(
        {"id", "name", "platform", "max_level", "limited_level", "multiplier", "is_treasure", "exclusive_set",
         "applicable_equipment"}
    );

    for (const auto &info : infos) {
        // exclusive_set: join with ;
        std::string excl_set;
        bool first = true;
        for (const auto &e : info.exclusive_set) {
            if (!first)
                excl_set += ";";
            first = false;
            excl_set += e.str();
        }

        // applicable_equipment: join with ;
        std::string app_eq;
        first = true;
        for (const auto &cat_nsid : info.applicable_equipments) {
            if (!first)
                app_eq += ";";
            first = false;
            auto cat_it = cat_reg.find(cat_nsid);
            std::string cat_name = cat_it != cat_reg.end() ? cat_it->name : "unknown";
            app_eq += cat_name;
        }

        table.push_back({
            info.id.str(),
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
    const std::vector<EnchInfo> &infos, const EquipmentTagRegistry &cat_reg,
    const std::filesystem::path &output_dir
) {
    for (const auto &info : infos) {
        // Split id into namespace and id
        auto [ns, id] = ParserUtils::split_namespace(info.id.str());

        // Construct output path: <output_dir>/data/<ns>/enchantment/<id>.json
        std::filesystem::path ench_dir = output_dir / "data" / ns / "enchantment";
        std::filesystem::create_directories(ench_dir);

        Json::Object obj;
        obj["anvil_cost"] = Json(Json::Number(static_cast<int32_t>(info.multiplier)));
        obj["max_level"]  = Json(Json::Number(static_cast<int32_t>(info.max_level)));

        // exclusive_set as namespaced IDs
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            std::string qualified = ParserUtils::qualify_id(e.str());
            excl.push_back(Json(Json::String(qualified)));
        }
        obj["exclusive_set"] = Json(excl);

        // supported_items — convert category NSIDs back to item IDs
        Json::Array supp;
        for (const auto &cat_nsid : info.applicable_equipments) {
            auto cat_it = cat_reg.find(cat_nsid);
            std::string cat_str = cat_it != cat_reg.end() ? cat_it->name : "unknown";
            // Avoid double-namespacing: cat may already contain "mod:item"
            if (cat_str.find(':') != std::string::npos) {
                supp.push_back(Json(Json::String(cat_str)));
            } else {
                supp.push_back(Json(Json::String("minecraft:" + cat_str)));
            }
        }
        obj["supported_items"] = Json(supp);

        // Write the file
        std::string json_str            = Json(obj).to_string(Json::Pretty);
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

std::string
EnchSerializer::to_json(const std::vector<Equipment> &equipments, const EquipmentTagRegistry &cat_reg) {
    Json::Array eq_arr;
    for (const auto &eq : equipments) {
        auto cat_it = cat_reg.find(eq.category);
        std::string cat_name = cat_it != cat_reg.end() ? cat_it->name : "unknown";
        Json::Object obj;
        obj["id"]             = Json(Json::String(eq.id.str()));
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

std::string
EnchSerializer::to_csv(const std::vector<Equipment> &equipments, const EquipmentTagRegistry &cat_reg) {
    csv::CsvTable table;
    table.push_back({"id", "name", "category", "max_durability"});

    for (const auto &eq : equipments) {
        auto cat_it = cat_reg.find(eq.category);
        std::string cat_name2 = cat_it != cat_reg.end() ? cat_it->name : "unknown";
        table.push_back({
            eq.id.str(),
            eq.name,
            cat_name2,
            std::to_string(eq.max_durability),
        });
    }

    return csv::format(table);
}

// ============================================================================
// Full-registry export
// ============================================================================

bool EnchSerializer::export_json(
    const std::string &path, const EnchantmentRegistry &ench_reg, const EquipmentRegistry &eq_reg,
    const EquipmentTagRegistry &cat_reg
) {
    // Collect valid entries (map stores items keyed by NSID, so all entries are valid)
    std::vector<EnchInfo> valid_ench;
    valid_ench.reserve(ench_reg.size());
    for (const auto &[nsid, info] : ench_reg.data())
        valid_ench.push_back(info);

    std::vector<Equipment> valid_eq;
    valid_eq.reserve(eq_reg.size());
    for (const auto &[id, eq] : eq_reg.data())
        valid_eq.push_back(eq);

    std::string ench_json = to_json(valid_ench, cat_reg);
    std::string eq_json   = to_json(valid_eq, cat_reg);

    Json::Object obj;
    obj["name"] = Json(Json::String("BestEnchSeq Registry Export"));

    // Extract inner arrays from the serialized JSON objects
    auto ench_root = Json::parse(ench_json);
    if (ench_root.is_valid()) {
        Json::Value root_val = ench_root.get_value();
        auto &root           = std::get<Json::Object>(root_val);
        auto it              = root.find("enchantments");
        if (it != root.end())
            obj["enchantments"] = it->second;
    }

    auto eq_root = Json::parse(eq_json);
    if (eq_root.is_valid()) {
        Json::Value root_val = eq_root.get_value();
        auto &root           = std::get<Json::Object>(root_val);
        auto it              = root.find("equipments");
        if (it != root.end())
            obj["equipments"] = it->second;
    }

    std::ofstream f(path);
    if (!f)
        return false;
    f << Json(obj).to_string(Json::Pretty);
    return true;
}

bool EnchSerializer::export_csv(
    const std::string &path, const EnchantmentRegistry &ench_reg, const EquipmentRegistry &eq_reg,
    const EquipmentTagRegistry &cat_reg
) {
    // Collect valid entries
    std::vector<EnchInfo> valid_ench;
    valid_ench.reserve(ench_reg.size());
    for (const auto &[nsid, info] : ench_reg.data())
        valid_ench.push_back(info);

    std::vector<Equipment> valid_eq;
    valid_eq.reserve(eq_reg.size());
    for (const auto &[id, eq] : eq_reg.data())
        valid_eq.push_back(eq);

    // Write enchantments CSV to the given path
    {
        std::string ench_csv = to_csv(valid_ench, cat_reg);
        std::ofstream f(path);
        if (!f)
            return false;
        f << ench_csv;
    }

    // Write equipment CSV to a sibling file
    std::filesystem::path p(path);
    auto eq_path = p.parent_path() / ("equipments_" + p.filename().string());
    {
        std::string eq_csv = to_csv(valid_eq, cat_reg);
        std::ofstream f_eq(eq_path);
        if (!f_eq)
            return false;
        f_eq << eq_csv;
    }

    return true;
}
