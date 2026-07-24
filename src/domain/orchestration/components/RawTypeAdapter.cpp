#include "RawTypeAdapter.h"
#include "common/CommonTypes.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================================
// Raw → domain helpers
// ============================================================================

std::vector<EnchInfo> RawTypeAdapter::resolve_ench_info(
    const std::vector<RawEnchantment>& raw,
    const EquipmentTagRegistry& tag_reg)
{
    std::vector<EnchInfo> result;
    result.reserve(raw.size());

    for (const auto& r : raw) {
        // Resolve applicable item strings -> NSIDs (from equipment tags)
        std::unordered_set<NSID> applicable_eq;
        applicable_eq.reserve(r.applicable_items.size());
        for (const auto& item_str : r.applicable_items) {
            auto nsid = NSID("#minecraft:" + item_str);
            auto it = tag_reg.find(nsid);
            if (it != tag_reg.end())
                applicable_eq.insert(it->id);
        }

        // platform and is_treasure are dropped from RawEnchantment:
        //   - platform defaults to All (cross-platform)
        //   - is_treasure is derived from limited_level == 0
        // Namespace-qualify exclusive_set entries (bare "breach" -> "minecraft:breach")
        // to match namespaced NSID convention.
        std::unordered_set<NSID> ns_exclusive_nsid;
        ns_exclusive_nsid.reserve(r.exclusive_set.size());
        for (const auto& excl : r.exclusive_set) {
            if (excl.find(':') == std::string::npos)
                ns_exclusive_nsid.insert(NSID("minecraft:" + excl));
            else
                ns_exclusive_nsid.insert(NSID(excl));
        }

        result.emplace_back(
            NSID(r.id.str()),
            r.display_name,
            MCE::All,
            r.max_level,
            r.limited_level,
            r.multiplier,
            r.limited_level == 0,
            std::move(ns_exclusive_nsid),
            std::move(applicable_eq)
        );
    }

    return result;
}

std::vector<Equipment> RawTypeAdapter::resolve_equipment(
    const std::vector<RawEquipment>& raw,
    const EquipmentTagRegistry& tag_reg)
{
    std::vector<Equipment> result;
    result.reserve(raw.size());

    for (const auto& r : raw) {
        NSID cat_nsid("#minecraft:" + r.category);
        auto cat_it = tag_reg.find(cat_nsid);

        result.emplace_back(Equipment{
            NSID(r.id.str()),
            r.display_name,
            cat_it != tag_reg.end() ? cat_it->id : NSID(),
            r.max_durability
        });
    }

    return result;
}

// ============================================================================
// RawTypeAdapter::resolve  (full pipeline)
// ============================================================================

void RawTypeAdapter::resolve(
    const std::vector<RawEnchantment>& enchants,
    const std::vector<RawEquipment>& equipments,
    EquipmentTagRegistry& tag_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg)
{
    // -- Step 1: Build EquipmentTagRegistry -----------------------------------
    // Collect unique category names from all equipment definitions and
    // initialize the tag registry with builtins + any custom categories.
    {
        std::unordered_set<std::string> seen;
        std::vector<std::string> custom_categories;
        custom_categories.reserve(equipments.size());
        for (const auto& eq : equipments) {
            if (seen.insert(eq.category).second)
                custom_categories.push_back(eq.category);
        }
        tag_reg.clear();
        for (const auto& name : custom_categories)
            tag_reg.insert({NSID("#minecraft:" + name), name});
    }

    // -- Step 2: Build EquipmentRegistry -------------------------------------
    {
        auto eq_list = resolve_equipment(equipments, tag_reg);
        for (auto& eq : eq_list)
            eq_reg.insert(eq);
    }

    // -- Step 3: Build EnchantmentRegistry -----------------------------------
    {
        auto ench_infos = resolve_ench_info(enchants, tag_reg);
        EnchantmentRegistry reg(std::move(ench_infos));
        ench_reg = std::move(reg);
    }
}

// ============================================================================
// Domain -> raw (revert)
// ============================================================================

void RawTypeAdapter::revert(
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg,
    const EquipmentTagRegistry& tag_reg,
    std::vector<RawEnchantment>& out_enchants,
    std::vector<RawEquipment>& out_equipments)
{
    // -- EnchantmentRegistry -> RawEnchantment[] ------------------------------
    const auto& ench_infos_map = ench_reg.data();
    out_enchants.reserve(ench_infos_map.size());
    for (const auto& [nsid, info] : ench_infos_map) {
        Id id;
        auto str_id = nsid.str();
        auto colon = str_id.find(':');
        if (colon != std::string::npos) {
            id.ns = str_id.substr(0, colon);
            id.path = str_id.substr(colon + 1);
        } else {
            id.ns = "minecraft";
            id.path = str_id;
        }

        // Equipment NSIDs -> string names
        std::unordered_set<std::string> applicable;
        for (const auto& eq_nsid : info.applicable_equipments) {
            auto tag_it = tag_reg.find(eq_nsid);
            if (tag_it != tag_reg.end())
                applicable.insert(tag_it->name);
            else
                applicable.insert(eq_nsid.str());
        }

        // Strip "minecraft:" prefix from exclusive_set namespaced IDs
        std::unordered_set<std::string> excl_bare;
        for (const auto& excl : info.exclusive_set) {
            auto excl_str = excl.str();
            auto ec = excl_str.find(':');
            if (ec != std::string::npos && excl_str.substr(0, ec) == "minecraft")
                excl_bare.insert(excl_str.substr(ec + 1));
            else
                excl_bare.insert(excl_str);
        }

        out_enchants.push_back({
            std::move(id),
            info.name,
            info.multiplier,
            info.max_level,
            info.limited_level,
            std::move(excl_bare),
            std::move(applicable)
        });
    }

    // -- EquipmentRegistry -> RawEquipment[] ----------------------------------
    const auto& eq_map = eq_reg.data();
    out_equipments.reserve(eq_map.size());
    for (const auto& [eq_nsid, eq] : eq_map) {
        Id id;
        auto str_id = eq_nsid.str();
        auto colon = str_id.find(':');
        if (colon != std::string::npos) {
            id.ns = str_id.substr(0, colon);
            id.path = str_id.substr(colon + 1);
        } else {
            id.ns = "minecraft";
            id.path = str_id;
        }

        std::string category_name;
        auto tag_it = tag_reg.find(eq.category);
        if (tag_it != tag_reg.end())
            category_name = tag_it->name;
        else
            category_name = "any";

        out_equipments.push_back({
            std::move(id),
            eq.name,
            std::move(category_name),
            eq.max_durability
        });
    }
}
