#include "parser/RegistryResolver.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EquipmentCategory.h"

#include <cstdint>
#include <unordered_set>

// ============================================================================

std::vector<EnchInfo> RegistryResolver::resolve_ench_info(
    const std::vector<RawEnchInfo> &raw,
    const EquipmentCategoryRegistry &cat_reg
) {
    std::vector<EnchInfo> result;
    result.reserve(raw.size());

    for (const auto &r : raw) {
        // Resolve applicable equipment strings → int32_t category IDs
        std::unordered_set<int32_t> category_ids;
        category_ids.reserve(r.applicable_equipment.size());
        for (const auto &eq_str : r.applicable_equipment) {
            int32_t cid = cat_reg.get_id(eq_str);
            if (cid >= 0)
                category_ids.insert(cid);
        }

        result.emplace_back(
            r.name_id,
            r.name,
            r.supported_platform,
            r.max_level,
            r.limited_level,
            r.multiplier,
            r.exclusive_set,     // already resolved strings (no further resolution needed)
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
            r.name_id,
            r.name,
            cat_id,
            r.max_durability
        });
    }

    return result;
}
