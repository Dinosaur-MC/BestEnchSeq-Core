#include "RawTypeAdapter.h"
#include "common/CommonTypes.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"

#include <cstdint>
#include <stdexcept>
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
            int32_t cid = tag_reg.get_id(item_str);
            if (cid >= 0)
                applicable_eq.insert(tag_reg.at(cid).id);
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
        int32_t cid = tag_reg.get_id(r.category);

        result.emplace_back(Equipment{
            NSID(r.id.str()),
            r.display_name,
            cid >= 0 ? tag_reg.at(cid).id : NSID(),
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
        tag_reg.initialize(custom_categories);
    }

    // -- Step 2: Build EquipmentRegistry -------------------------------------
    {
        auto eq_list = resolve_equipment(equipments, tag_reg);
        eq_reg.initialize(std::move(eq_list));
    }

    // -- Step 3: Build EnchantmentRegistry -----------------------------------
    {
        auto ench_infos = resolve_ench_info(enchants, tag_reg);
        ench_reg.initialize(std::move(ench_infos));
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
    const auto& ench_infos = ench_reg.get_instances();
    out_enchants.reserve(ench_infos.size());
    for (const auto& info : ench_infos) {
        Id id;
        auto str_id = info.id.str();
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
            bool found = false;
            for (size_t i = 0; i < tag_reg.size(); ++i) {
                if (tag_reg.at(i).id == eq_nsid) {
                    applicable.insert(tag_reg.at(i).name);
                    found = true;
                    break;
                }
            }
            if (!found)
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
    const auto& eq_instances = eq_reg.get_instances();
    out_equipments.reserve(eq_instances.size());
    for (const auto& eq : eq_instances) {
        Id id;
        auto str_id = eq.id.str();
        auto colon = str_id.find(':');
        if (colon != std::string::npos) {
            id.ns = str_id.substr(0, colon);
            id.path = str_id.substr(colon + 1);
        } else {
            id.ns = "minecraft";
            id.path = str_id;
        }

        std::string category_name;
        bool found = false;
        for (size_t i = 0; i < tag_reg.size(); ++i) {
            if (tag_reg.at(i).id == eq.category) {
                category_name = tag_reg.at(i).name;
                found = true;
                break;
            }
        }
        if (!found)
            category_name = "any";

        out_equipments.push_back({
            std::move(id),
            eq.name,
            std::move(category_name),
            eq.max_durability
        });
    }
}
