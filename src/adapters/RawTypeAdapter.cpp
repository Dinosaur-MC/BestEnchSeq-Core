#include "adapters/RawTypeAdapter.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include "types/Platform.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================================
// RawTypeAdapter::resolve
// ============================================================================

RawTypeAdapter::ResolvedRegistries RawTypeAdapter::resolve(
    const std::vector<RawEnchantment>& enchants,
    const std::vector<RawEquipment>& equipments)
{
    ResolvedRegistries result;

    // ── Step 1: Build EquipmentCategoryRegistry ──────────────────────────
    // Collect unique category names from all equipment definitions.
    {
        std::vector<std::string> custom_categories;
        std::unordered_set<std::string> seen;
        for (const auto& eq : equipments) {
            if (seen.insert(eq.category).second)
                custom_categories.push_back(eq.category);
        }
        result.cat_reg.initialize(custom_categories);
    }

    // ── Step 2: Build EquipmentRegistry ─────────────────────────────────
    {
        std::vector<Equipment> eq_list;
        eq_list.reserve(equipments.size());

        for (const auto& raw_eq : equipments) {
            int32_t cat_id = result.cat_reg.get_id(raw_eq.category);
            // cat_id is always valid because we just registered all categories.

            eq_list.push_back(Equipment{
                raw_eq.id.str(),
                raw_eq.display_name,
                cat_id,
                raw_eq.max_durability
            });
        }

        result.eq_reg.initialize(eq_list);
    }

    // ── Step 3: Build EnchantmentRegistry ────────────────────────────────
    {
        std::vector<EnchInfo> ench_list;
        ench_list.reserve(enchants.size());

        for (const auto& raw_ench : enchants) {
            EnchInfo info;

            info.name_id       = raw_ench.id.str();
            info.name          = raw_ench.display_name;
            info.supported_platform = MCE::All;
            info.max_level     = raw_ench.max_level;
            info.limited_level = raw_ench.limited_level;
            info.multiplier    = raw_ench.multiplier;
            info.is_treasure   = (raw_ench.limited_level == 0);

            // Pass through exclusive_set as-is (string enchantment names).
            // The EnchantmentRegistry will resolve these to int32_t IDs during
            // initialize().
            info.exclusive_set = raw_ench.exclusive_set;

            // Resolve applicable_items strings → category int32_t IDs.
            info.applicable_category_ids.reserve(raw_ench.applicable_items.size());
            for (const auto& item_str : raw_ench.applicable_items) {
                int32_t cid = result.cat_reg.get_id(item_str);
                if (cid >= 0)
                    info.applicable_category_ids.insert(cid);
            }

            ench_list.push_back(std::move(info));
        }

        result.ench_reg.initialize(ench_list);
    }

    return result;
}
