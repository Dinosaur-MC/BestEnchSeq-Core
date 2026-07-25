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
        root["name"]        = Json(metadata->name);
        root["description"] = Json(metadata->description);
        root["author"]      = Json(metadata->author);
        root["version"]     = Json(metadata->version);
    }

    // Build enchantments array
    Json::Array enchants;
    for (const auto &info : infos) {
        Json::Object obj;
        obj["id"]            = Json(info.id.str());
        obj["name"]          = Json(info.name);
        obj["platform"]      = Json(ParserUtils::platform_to_string(info.supported_platform));
        obj["max_level"]     = Json(info.max_level);
        obj["limited_level"] = Json(info.limited_level);
        obj["multiplier"]    = Json(info.multiplier);
        obj["is_treasure"]   = Json(info.is_treasure);

        // exclusive_set array
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            excl.push_back(Json(e.str()));
        }
        obj["exclusive_set"] = Json(excl);

        // applicable_equipment array
        Json::Array eq;
        for (const auto &cat_nsid : info.applicable_equipments) {
            auto cat_it = cat_reg.find(cat_nsid);
            std::string cat_name = cat_it != cat_reg.end() ? cat_it->name : "unknown";
            eq.push_back(Json(cat_name));
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
        obj["anvil_cost"] = Json(info.multiplier);
        obj["max_level"]  = Json(info.max_level);

        // exclusive_set as namespaced IDs
        Json::Array excl;
        for (const auto &e : info.exclusive_set) {
            std::string qualified = ParserUtils::qualify_id(e.str());
            excl.push_back(Json(qualified));
        }
        obj["exclusive_set"] = Json(excl);

        // supported_items — convert category NSIDs back to item IDs
        Json::Array supp;
        for (const auto &cat_nsid : info.applicable_equipments) {
            auto cat_it = cat_reg.find(cat_nsid);
            std::string cat_str = cat_it != cat_reg.end() ? cat_it->name : "unknown";
            // Avoid double-namespacing: cat may already contain "mod:item"
            if (cat_str.find(':') != std::string::npos) {
                supp.push_back(Json(cat_str));
            } else {
                supp.push_back(Json("minecraft:" + cat_str));
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
        obj["id"]             = Json(eq.id.str());
        obj["name"]           = Json(eq.name);
        obj["category"]       = Json(cat_name);
        obj["max_durability"] = Json(eq.max_durability);
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
// Profile-aware export
// ============================================================================

bool EnchSerializer::export_json(
    const std::string& path,
    const Profile& profile)
{
    std::vector<EnchInfo> valid_ench;
    valid_ench.reserve(profile.ench().size());
    for (const auto& [nsid, info] : profile.ench().data())
        valid_ench.push_back(info);

    std::vector<Equipment> valid_eq;
    valid_eq.reserve(profile.eq().size());
    for (const auto& [id, eq] : profile.eq().data())
        valid_eq.push_back(eq);

    std::string ench_json = to_json(valid_ench, profile);
    std::string eq_json   = to_json(valid_eq, profile);

    Json::Object obj;
    obj["name"] = Json("BestEnchSeq Registry Export");

    auto ench_root = Json::parse(ench_json);
    if (ench_root.is_valid()) {
        if (ench_root.has("enchantments"))
            obj["enchantments"] = ench_root["enchantments"];
    }

    auto eq_root = Json::parse(eq_json);
    if (eq_root.is_valid()) {
        if (eq_root.has("equipments"))
            obj["equipments"] = eq_root["equipments"];
    }

    std::ofstream f(path);
    if (!f) return false;
    f << Json(obj).to_string(Json::Pretty);
    return true;
}

bool EnchSerializer::export_csv(
    const std::string& path,
    const Profile& profile)
{
    std::vector<EnchInfo> valid_ench;
    valid_ench.reserve(profile.ench().size());
    for (const auto& [nsid, info] : profile.ench().data())
        valid_ench.push_back(info);

    std::vector<Equipment> valid_eq;
    valid_eq.reserve(profile.eq().size());
    for (const auto& [id, eq] : profile.eq().data())
        valid_eq.push_back(eq);

    std::string ench_csv = to_csv(valid_ench, profile);
    std::ofstream f(path);
    if (!f) return false;
    f << ench_csv;

    std::filesystem::path p(path);
    auto eq_path = p.parent_path() / ("equipments_" + p.filename().string());
    std::string eq_csv = to_csv(valid_eq, profile);
    std::ofstream f_eq(eq_path);
    if (!f_eq) return false;
    f_eq << eq_csv;

    return true;
}

// ── Profile-aware delegates ─────────────────────────────────────────────

std::string EnchSerializer::to_json(
    const std::vector<EnchInfo>& infos,
    const Profile& profile,
    const EnchantmentDataPack* metadata)
{
    return to_json(infos, profile.tags(), metadata);
}

std::string EnchSerializer::to_csv(
    const std::vector<EnchInfo>& infos,
    const Profile& profile)
{
    return to_csv(infos, profile.tags());
}

std::string EnchSerializer::to_json(
    const std::vector<Equipment>& equipments,
    const Profile& profile)
{
    return to_json(equipments, profile.tags());
}

std::string EnchSerializer::to_csv(
    const std::vector<Equipment>& equipments,
    const Profile& profile)
{
    return to_csv(equipments, profile.tags());
}

// ── Profile-aware MC official export ────────────────────────────────────

void EnchSerializer::export_to_mc_official(
    const std::filesystem::path& output_dir,
    const Profile& profile)
{
    std::vector<EnchInfo> infos;
    infos.reserve(profile.ench().size());
    for (const auto& [nsid, info] : profile.ench().data())
        infos.push_back(info);
    export_to_mc_official(infos, profile.tags(), output_dir);
}
