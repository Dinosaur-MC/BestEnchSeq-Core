#include "adapters/RegistryResolver.h"
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
        result.emplace_back(
            r.id.str(),
            r.display_name,
            MCE::All,
            r.max_level,
            r.limited_level,
            r.multiplier,
            r.limited_level == 0,
            r.exclusive_set,       // already resolved strings
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

// ============================================================================
// Enchantment name resolution
// ============================================================================

int32_t RegistryResolver::resolve_ench_id(
    const std::string &name,
    const EnchantmentRegistry &ench_reg
) {
    int32_t id = ench_reg.get_id(name);
    if (id < 0) {
        id = ench_reg.get_id("minecraft:" + name);
    }
    return id;
}

// ============================================================================

int32_t RegistryResolver::resolve_ench_id(
    const std::string &ns,
    const std::string &id,
    const EnchantmentRegistry &ench_reg
) {
    std::string namespaced = id.find(':') != std::string::npos ? id : ns + ":" + id;
    int32_t ench_id = ench_reg.get_id(namespaced);
    if (ench_id >= 0) return ench_id;

    // Fallback: try bare id (for data registered without namespace prefix)
    ench_id = ench_reg.get_id(id);
    if (ench_id >= 0) return ench_id;

    throw std::runtime_error("Unknown enchantment: " + namespaced);
}

// ============================================================================
// Raw data merging
// ============================================================================

void RegistryResolver::merge_raw_ench_info(
    std::vector<RawEnchantment> &base,
    const std::vector<RawEnchantment> &extra
) {
    // Build set of existing ids for O(1) dedup
    std::unordered_set<std::string> existing;
    existing.reserve(base.size());
    for (const auto &r : base)
        existing.insert(r.id.str());

    for (const auto &r : extra) {
        auto key = r.id.str();
        if (existing.insert(key).second)
            base.push_back(r);
    }
}

void RegistryResolver::merge_raw_equipment(
    std::vector<RawEquipment> &base,
    const std::vector<RawEquipment> &extra
) {
    std::unordered_set<std::string> existing;
    existing.reserve(base.size());
    for (const auto &r : base)
        existing.insert(r.id.str());

    for (const auto &r : extra) {
        auto key = r.id.str();
        if (existing.insert(key).second)
            base.push_back(r);
    }
}
