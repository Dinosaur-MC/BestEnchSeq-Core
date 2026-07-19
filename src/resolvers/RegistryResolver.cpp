#include "resolvers/RegistryResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EquipmentCategory.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

// ============================================================================
// Raw → domain type resolution
// ============================================================================

std::vector<EnchInfo> RegistryResolver::resolve_ench_info(
    const std::vector<RawEnchantment> &raw,
    const EquipmentCategoryRegistry &cat_reg
) {
    std::vector<EnchInfo> result;
    result.reserve(raw.size());

    for (const auto &r : raw) {
        // Resolve applicable item strings → int32_t category IDs
        std::unordered_set<int32_t> category_ids;
        category_ids.reserve(r.applicable_items.size());
        for (const auto &item_str : r.applicable_items) {
            int32_t cid = cat_reg.get_id(item_str);
            if (cid >= 0)
                category_ids.insert(cid);
        }

        // platform and is_treasure are dropped from RawEnchantment:
        //   - platform defaults to All (cross-platform)
        //   - is_treasure is derived from limited_level == 0
        // Namespace-qualify exclusive_set entries (bare "breach" → "minecraft:breach")
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

// ============================================================================

std::vector<Equipment> RegistryResolver::resolve_equipment(
    const std::vector<RawEquipment> &raw,
    const EquipmentCategoryRegistry &cat_reg
) {
    std::vector<Equipment> result;
    result.reserve(raw.size());

    for (const auto &r : raw) {
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

