#include "RegistryLoader.h"
#include "domain/business/components/Serializer.h"
#include "common/CommonTypes.h"
#include "common/log/log.hpp"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================================
// DTO → Registry
// ============================================================================

void RegistryLoader::from_dto(
    EnchantmentRegistry& reg,
    const TagRegistry& tag_reg,
    const EquipmentRegistry& eq_reg,
    const std::vector<business::loader::EnchantmentData>& data)
{
    for (const auto& d : data) {
        // supported_items: 原始引用交叉验证（#tag 需定义，具体 ID 需存在）
        std::unordered_set<NSID> supported;
        for (const auto& ref : d.applicable_to) {
            if (!ref.empty() && ref[0] == '#') {
                NSID tag_nsid(ref);
                if (tag_reg.contains(tag_nsid))
                    supported.insert(tag_nsid);          // 保留
            } else {
                NSID item_nsid = (ref.find(':') == std::string::npos) ? NSID("minecraft:" + ref) : NSID(ref);
                if (eq_reg.contains(item_nsid))
                    supported.insert(item_nsid);          // 保留
            }
        }
        if (supported.empty()) {
            LOG_WARN("Skipping enchantment '%s': no resolvable supported_items", d.id.c_str());
            continue;   // 空 supported_items 的魔咒移除
        }

        // exclusive_with 命名空间化（不变）
        std::unordered_set<NSID> exclusive_nsid;
        for (const auto& excl : d.exclusive_with) {
            if (excl.find(':') == std::string::npos)
                exclusive_nsid.insert(NSID("minecraft:" + excl));
            else
                exclusive_nsid.insert(NSID(excl));
        }

        EnchInfo info;
        info.id                = NSID(d.id);
        info.name              = d.display_name;
        info.supported_platform = MCE::All;
        info.max_level         = d.max_level;
        info.limited_level     = d.limited_level;
        info.multiplier        = d.multiplier;
        info.is_treasure       = (d.limited_level == 0);
        info.exclusive_set     = std::move(exclusive_nsid);
        info.supported_items   = std::move(supported);
        reg.insert(std::move(info));
    }
}

void RegistryLoader::from_dto(
    EquipmentRegistry& reg,
    const TagRegistry& tag_reg,
    const std::vector<business::loader::EquipmentData>& data)
{
    for (const auto& d : data) {
        NSID cat_nsid("#minecraft:" + d.category);
        auto cat_it = tag_reg.find(cat_nsid);

        Equipment eq;
        eq.id             = NSID(d.id);
        eq.name           = d.display_name;
        eq.category       = (cat_it != tag_reg.end()) ? cat_it->id : NSID();
        eq.max_durability = d.max_durability;

        reg.insert(std::move(eq));
    }
}

// ============================================================================
// Json → Registry
// ============================================================================

bool RegistryLoader::from_json(EnchantmentRegistry& reg, const Json& json) {
    if (json.type() == JsonType::Null || json.type() == JsonType::Empty)
        return false;
    try {
        json >> reg;
        return true;
    } catch (...) {
        return false;
    }
}

bool RegistryLoader::from_json(EquipmentRegistry& reg, const Json& json) {
    if (json.type() == JsonType::Null || json.type() == JsonType::Empty)
        return false;
    try {
        json >> reg;
        return true;
    } catch (...) {
        return false;
    }
}

bool RegistryLoader::from_json(TagRegistry& reg, const Json& json) {
    if (json.type() == JsonType::Null || json.type() == JsonType::Empty)
        return false;
    try {
        json >> reg;
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Registry → Json
// ============================================================================

Json RegistryLoader::to_json(const EnchantmentRegistry& reg) {
    Json j;
    j << reg;
    return j;
}

Json RegistryLoader::to_json(const EquipmentRegistry& reg) {
    Json j;
    j << reg;
    return j;
}

Json RegistryLoader::to_json(const TagRegistry& reg) {
    Json j;
    j << reg;
    return j;
}

// ============================================================================
// Registry → DTO
// ============================================================================

std::vector<business::loader::EnchantmentData> RegistryLoader::to_dto(
    const EnchantmentRegistry& reg,
    const TagRegistry& tag_reg)
{
    std::vector<business::loader::EnchantmentData> result;
    result.reserve(reg.size());

    for (const auto& [nsid, info] : reg.data()) {
        // Equipment NSIDs → category name strings
        std::vector<std::string> applicable;
        for (const auto& eq_nsid : info.supported_items) {
            auto tag_it = tag_reg.find(eq_nsid);
            if (tag_it != tag_reg.end())
                applicable.push_back(tag_it->name);
            else
                applicable.push_back(eq_nsid.str());
        }

        // Strip "minecraft:" prefix from exclusive_set NSIDs
        std::vector<std::string> exclusive_bare;
        for (const auto& excl : info.exclusive_set) {
            auto excl_str = excl.str();
            auto ec = excl_str.find(':');
            if (ec != std::string::npos && excl_str.substr(0, ec) == "minecraft")
                exclusive_bare.push_back(excl_str.substr(ec + 1));
            else
                exclusive_bare.push_back(excl_str);
        }

        business::loader::EnchantmentData d;
        d.id               = nsid.str();
        d.display_name     = info.name;
        d.multiplier       = info.multiplier;
        d.max_level        = info.max_level;
        d.limited_level    = info.limited_level;
        d.exclusive_with   = std::move(exclusive_bare);
        d.applicable_to    = std::move(applicable);
        result.push_back(std::move(d));
    }

    return result;
}

std::vector<business::loader::EquipmentData> RegistryLoader::to_dto(
    const EquipmentRegistry& reg,
    const TagRegistry& tag_reg)
{
    std::vector<business::loader::EquipmentData> result;
    result.reserve(reg.size());

    for (const auto& [eq_nsid, eq] : reg.data()) {
        std::string category_name;
        auto tag_it = tag_reg.find(eq.category);
        if (tag_it != tag_reg.end())
            category_name = tag_it->name;
        else
            category_name = "any";

        business::loader::EquipmentData d;
        d.id             = eq_nsid.str();
        d.display_name   = eq.name;
        d.category       = std::move(category_name);
        d.max_durability = eq.max_durability;
        result.push_back(std::move(d));
    }

    return result;
}

// ============================================================================
// Full pipeline
// ============================================================================

void RegistryLoader::resolve(
    const std::vector<business::loader::EnchantmentData>& enchants,
    const std::vector<business::loader::EquipmentData>& equipments,
    TagRegistry& tag_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg,
    const TagRegistry* base_tags)
{
    // Step 1: Build TagRegistry.  Seeded ONLY from the base tags (vanilla
    // fallback) — no synthetic `#minecraft:<category>` tags are derived from
    // the equipment data anymore.  A `#tag` supported_items reference only
    // resolves if it is defined here.
    tag_reg.clear();
    if (base_tags) {
        for (const auto& [id, tag] : base_tags->data())
            tag_reg.insert(tag);
    }

    // Step 2: Build EquipmentRegistry
    {
        from_dto(eq_reg, tag_reg, equipments);
    }

    // Step 3: Build EnchantmentRegistry
    {
        from_dto(ench_reg, tag_reg, eq_reg, enchants);
    }
}
