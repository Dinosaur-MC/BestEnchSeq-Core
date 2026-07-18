#include "adapters/RawTypeAdapter.h"
#include "adapters/RegistryResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EnchInfo.h"
#include "types/Equipment.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================================
// RawTypeAdapter::resolve
// ============================================================================

void RawTypeAdapter::resolve(
    const std::vector<RawEnchantment>& enchants,
    const std::vector<RawEquipment>& equipments,
    EquipmentCategoryRegistry& cat_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg)
{
    // ── Step 1: Build EquipmentCategoryRegistry ──────────────────────────
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

    // ── Step 2: Build EquipmentRegistry ─────────────────────────────────
    {
        auto eq_list = RegistryResolver::resolve_equipment(equipments, cat_reg);
        eq_reg.initialize(eq_list);
    }

    // ── Step 3: Build EnchantmentRegistry ────────────────────────────────
    {
        auto ench_infos = RegistryResolver::resolve_ench_info(enchants, cat_reg);
        ench_reg.initialize(ench_infos);
    }
}
