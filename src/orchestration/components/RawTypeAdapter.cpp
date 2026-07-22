#include "RawTypeAdapter.h"
#include "business-domain/types/Equipment.h"

#include <algorithm>
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
    const EquipmentCategoryRegistry& cat_reg)
{
    std::vector<EnchInfo> result;
    result.reserve(raw.size());

    for (const auto& r : raw) {
        // Resolve applicable item strings -> int32_t category IDs
        std::unordered_set<int32_t> category_ids;
        category_ids.reserve(r.applicable_items.size());
        for (const auto& item_str : r.applicable_items) {
            int32_t cid = cat_reg.get_id(item_str);
            if (cid >= 0)
                category_ids.insert(cid);
        }

        // platform and is_treasure are dropped from RawEnchantment:
        //   - platform defaults to All (cross-platform)
        //   - is_treasure is derived from limited_level == 0
        // Namespace-qualify exclusive_set entries (bare "breach" -> "minecraft:breach")
        // to match namespaced name_id convention.
        std::unordered_set<std::string> ns_exclusive;
        ns_exclusive.reserve(r.exclusive_set.size());
        for (const auto& excl : r.exclusive_set) {
            if (excl.find(':') == std::string::npos)
                ns_exclusive.insert("minecraft:" + excl);
            else
                ns_exclusive.insert(excl);
        }

        result.emplace_back(
            r.id.str(),
            r.display_name,
            MCE::All,
            r.max_level,
            r.limited_level,
            r.multiplier,
            r.limited_level == 0,
            std::move(ns_exclusive),
            std::move(category_ids)
        );
    }

    return result;
}

std::vector<Equipment> RawTypeAdapter::resolve_equipment(
    const std::vector<RawEquipment>& raw,
    const EquipmentCategoryRegistry& cat_reg)
{
    std::vector<Equipment> result;
    result.reserve(raw.size());

    for (const auto& r : raw) {
        int32_t cat_id = cat_reg.get_id(r.category);
        if (cat_id < 0)
            cat_id = EquipmentCategory::ID_ANY;

        result.emplace_back(Equipment{
            r.id.str(),
            r.display_name,
            cat_id,
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
    EquipmentCategoryRegistry& cat_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg)
{
    // -- Step 1: Build EquipmentCategoryRegistry ------------------------------
    // Collect unique category names from all equipment definitions and
    // initialize the category registry with builtins + any custom categories.
    {
        std::unordered_set<std::string> seen;
        std::vector<std::string> custom_categories;
        custom_categories.reserve(equipments.size());
        for (const auto& eq : equipments) {
            if (seen.insert(eq.category).second)
                custom_categories.push_back(eq.category);
        }
        cat_reg.initialize(custom_categories);
    }

    // -- Step 2: Build EquipmentRegistry -------------------------------------
    {
        auto eq_list = resolve_equipment(equipments, cat_reg);
        eq_reg.initialize(std::move(eq_list));
    }

    // -- Step 3: Build EnchantmentRegistry -----------------------------------
    {
        auto ench_infos = resolve_ench_info(enchants, cat_reg);
        ench_reg.initialize(std::move(ench_infos));
    }
}

// ============================================================================
// Domain -> raw (revert)
// ============================================================================

void RawTypeAdapter::revert(
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg,
    const EquipmentCategoryRegistry& cat_reg,
    std::vector<RawEnchantment>& out_enchants,
    std::vector<RawEquipment>& out_equipments)
{
    // -- EnchantmentRegistry -> RawEnchantment[] ------------------------------
    const auto& ench_infos = ench_reg.get_instances();
    out_enchants.reserve(ench_infos.size());
    for (const auto& info : ench_infos) {
        Id id;
        auto colon = info.name_id.find(':');
        if (colon != std::string::npos) {
            id.ns = info.name_id.substr(0, colon);
            id.path = info.name_id.substr(colon + 1);
        } else {
            id.ns = "minecraft";
            id.path = info.name_id;
        }

        // Category IDs -> string names
        std::unordered_set<std::string> applicable;
        for (int32_t cid : info.applicable_category_ids) {
            try {
                applicable.insert(cat_reg.get(cid).name_id);
            } catch (const std::out_of_range&) {
                // Skip unknown category IDs silently
            }
        }

        // Strip "minecraft:" prefix from exclusive_set namespaced IDs
        std::unordered_set<std::string> excl_bare;
        for (const auto& excl : info.exclusive_set) {
            auto ec = excl.find(':');
            if (ec != std::string::npos && excl.substr(0, ec) == "minecraft")
                excl_bare.insert(excl.substr(ec + 1));
            else
                excl_bare.insert(excl);
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

    // -- EquipmentRegistry -> RawEquipment[] ---------------------------------
    const auto& eq_instances = eq_reg.get_instances();
    out_equipments.reserve(eq_instances.size());
    for (const auto& eq : eq_instances) {
        Id id;
        auto colon = eq.name_id.find(':');
        if (colon != std::string::npos) {
            id.ns = eq.name_id.substr(0, colon);
            id.path = eq.name_id.substr(colon + 1);
        } else {
            id.ns = "minecraft";
            id.path = eq.name_id;
        }

        std::string category_name;
        try {
            category_name = cat_reg.get(eq.category_id).name_id;
        } catch (const std::out_of_range&) {
            category_name = "any";
        }

        out_equipments.push_back({
            std::move(id),
            eq.name,
            std::move(category_name),
            eq.max_durability
        });
    }
}
