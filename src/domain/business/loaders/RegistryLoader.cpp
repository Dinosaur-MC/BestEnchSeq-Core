#include "RegistryLoader.h"
#include "domain/business/components/Serializer.h"
#include "domain/business/parsers/ParserShared.h"
#include "BuiltinData.h"
#include "common/CommonTypes.h"
#include "common/log/log.hpp"
#include "common/utils/StringUtils.hpp"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

/// Content-equality for duplicate-NSID conflict detection.  EnchInfo/Equipment
/// operator== compares ONLY the id (registry key), so a plain duplicate (a
/// redefinition with identical content — very common, e.g. a datapack
/// re-declaring a vanilla entry) cannot be told apart from a CONFLICTING one
/// (same id, different data).  Only the latter deserves a warning.
bool ench_content_equal(const EnchInfo& a, const EnchInfo& b) {
    // Name compared case-insensitively: display names from different sources
    // legitimately differ in case (vanilla Title Case "Copper Boots" vs
    // datapack-derived sentence case "Copper boots"); a case-only difference
    // is representation noise, NOT a content conflict.
    return string_utils::iequals(a.name, b.name) &&
           a.supported_platform == b.supported_platform &&
           a.max_level == b.max_level && a.limited_level == b.limited_level &&
           a.limited_level_provided == b.limited_level_provided &&
           a.multiplier == b.multiplier && a.is_treasure == b.is_treasure &&
           a.exclusive_set == b.exclusive_set && a.supported_items == b.supported_items &&
           a.min_cost_base == b.min_cost_base && a.min_cost_per_level == b.min_cost_per_level;
}

bool equip_content_equal(const Equipment& a, const Equipment& b) {
    return string_utils::iequals(a.name, b.name) && a.category == b.category &&
           a.max_durability == b.max_durability;
}

/// FIELD-LEVEL merge for duplicate NSIDs: NEW WINS per field, but only for
/// fields the new entry actually PROVIDED.  Datapack entries derived from
/// item tags can be partial (e.g. max_durability=0 when the item is missing
/// from item_properties, or an empty display name); a whole-entry replace
/// would clobber the vanilla values with empty/defaults.  Absent fields are
/// treated as "not provided" and keep the old value:
///   strings:   empty            → keep old
///   numbers:   <= 0             → keep old
///   sets:      empty            → keep old (cannot distinguish intentional
///              clearing from "not provided")
///   is_treasure:false           → keep old (same ambiguity; true still wins)
///   platform:  None             → keep old
/// A new name that differs from the old one ONLY in case (datapack-derived
/// sentence case vs vanilla Title Case) keeps the old (curated) name — it is
/// representation noise, not a rename.
EnchInfo merge_ench(const EnchInfo& old, const EnchInfo& in) {
    EnchInfo out = old;
    if (!in.name.empty() && !string_utils::iequals(in.name, old.name)) out.name = in.name;
    if (in.supported_platform != MCE::None) out.supported_platform = in.supported_platform;
    if (in.max_level > 0) out.max_level = in.max_level;
    if (in.limited_level_provided) {
        out.limited_level = in.limited_level;
        out.limited_level_provided = true;
    }
    if (in.multiplier > 0) out.multiplier = in.multiplier;
    if (in.is_treasure) out.is_treasure = true;
    if (!in.exclusive_set.empty()) out.exclusive_set = in.exclusive_set;
    if (!in.supported_items.empty()) out.supported_items = in.supported_items;
    if (in.min_cost_base > 0) out.min_cost_base = in.min_cost_base;
    if (in.min_cost_per_level > 0) out.min_cost_per_level = in.min_cost_per_level;
    return out;
}

Equipment merge_equip(const Equipment& old, const Equipment& in) {
    Equipment out = old;
    // Case-only name differences keep the old (curated) name — see merge_ench.
    if (!in.name.empty() && !string_utils::iequals(in.name, old.name)) out.name = in.name;
    if (!in.category.str().empty()) out.category = in.category;
    if (in.max_durability > 0) out.max_durability = in.max_durability;
    return out;
}

} // namespace

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
        size_t dropped = 0;
        for (const auto& ref : d.applicable_to) {
            if (ref.empty()) {          // 空引用不能构成 NSID —— 跳过
                ++dropped;
                continue;
            }
            if (ref[0] == '#') {
                NSID tag_nsid(ref);
                if (tag_reg.contains(tag_nsid))
                    supported.insert(tag_nsid);          // 保留
                else
                    ++dropped;
            } else {
                NSID item_nsid = (ref.find(':') == std::string::npos) ? NSID("minecraft:" + ref) : NSID(ref);
                if (eq_reg.contains(item_nsid))
                    supported.insert(item_nsid);          // 保留
                else
                    ++dropped;
            }
        }
        if (supported.empty()) {
            LOG_WARN("Skipping enchantment '%s': no resolvable supported_items (dropped refs: %zu/%zu)",
                     d.id.c_str(), dropped, d.applicable_to.size());
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
        info.supported_platform = d.platform.empty() ? MCE::All
                                                     : Serializer::string_to_mce(d.platform);
        info.max_level         = d.max_level;
        info.limited_level     = d.limited_level;
        info.limited_level_provided = d.limited_level_provided;
        info.multiplier        = d.multiplier;
        info.min_cost_base     = d.min_cost_base;
        info.min_cost_per_level = d.min_cost_per_level;
        info.is_treasure       = d.is_treasure;   // 数据值（解析自 vanilla.json / datapack treasure tag）
        info.exclusive_set     = std::move(exclusive_nsid);
        info.supported_items   = std::move(supported);
        // NEW WINS over OLD, FIELD-LEVEL: provided fields replace, absent
        // fields (empty/0/false) keep the old value.  Content-identical
        // redefinitions are silent; a CONFLICTING replacement warns.
        // The FINAL value is validated before it may enter the registry — a
        // still-invalid entry (even after merging) is dropped, never stored.
        EnchInfo final_ench = std::move(info);
        bool conflicting = false;
        if (auto it = reg.find(final_ench.id); it != reg.end()) {
            final_ench = merge_ench(*it, final_ench);
            conflicting = !ench_content_equal(*it, final_ench);
        }
        if (final_ench.max_level <= 0 || final_ench.multiplier <= 0) {
            LOG_WARN("Skipping enchantment '%s': invalid max_level=%d multiplier=%d — not registered",
                     final_ench.id.str().c_str(), final_ench.max_level, final_ench.multiplier);
            continue;
        }
        reg.insert_or_assign(final_ench);
        if (conflicting)
            LOG_WARN("Replacing existing enchantment '%s' with conflicting data (new entry wins)",
                     d.id.c_str());
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
        // `category` is a display short name (e.g. "sword" → `#minecraft:sword`).
        // It is NOT a registered tag — the real MC item tags (`#minecraft:swords`,
        // `#minecraft:enchantable/sharp_weapon`) are the applicability source of
        // truth.  Keep the NSID as the informational label when the tag is not
        // defined so display/serialization don't lose the category (T10).
        eq.category       = (cat_it != tag_reg.end()) ? cat_it->id : cat_nsid;
        eq.max_durability = d.max_durability;

        // NEW WINS over OLD, FIELD-LEVEL (same semantics as enchantments):
        // partial datapack derivations (empty name / durability 0) must not
        // clobber the vanilla fields.  The FINAL value is validated before
        // registration — but "no durability" is NOT invalid by itself:
        // newer MC items AND mod items can be enchantable with no max_damage
        // component (durability 0, category possibly underivable).  Only an
        // entry with NO durability, NO category AND NO name (nothing usable
        // about it at all) is dropped as junk.
        Equipment final_eq = std::move(eq);
        bool conflicting = false;
        if (auto it = reg.find(final_eq.id); it != reg.end()) {
            final_eq = merge_equip(*it, final_eq);
            conflicting = !equip_content_equal(*it, final_eq);
        }
        const auto colon = final_eq.id.str().find(':');
        const std::string id_suffix =
            colon == std::string::npos ? final_eq.id.str() : final_eq.id.str().substr(colon + 1);
        const bool uncategorized = final_eq.category.str() == "#minecraft:" + id_suffix;
        if (final_eq.max_durability <= 0 && uncategorized && final_eq.name.empty()) {
            LOG_WARN("Skipping equipment '%s': no durability, no category, no name — not registered",
                     final_eq.id.str().c_str());
            continue;
        }
        reg.insert_or_assign(final_eq);
        if (conflicting)
            LOG_WARN("Replacing existing equipment '%s' with conflicting data (new entry wins)",
                     d.id.c_str());
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
        // supported_items references are emitted RAW (`#tag` or concrete id)
        // so a to_dto → from_dto round-trip preserves reference semantics
        // under cross-validation (a bare category name would be re-interpreted
        // as a concrete item id).
        (void)tag_reg;
        std::vector<std::string> applicable;
        for (const auto& eq_nsid : info.supported_items)
            applicable.push_back(eq_nsid.str());

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
        d.limited_level_provided = info.limited_level_provided;
        d.min_cost_base    = info.min_cost_base;
        d.min_cost_per_level = info.min_cost_per_level;
        d.is_treasure      = info.is_treasure;
        d.platform         = std::string(Serializer::mce_to_string(info.supported_platform));
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
        else {
            // Category is a display-only short name under the real-MC-tag model
            // (T10): fall back to the NSID's short form so a to_dto → from_dto
            // round-trip keeps the display category.
            category_name = business::parser_detail::category_short_name(eq.category);
            if (category_name.empty())
                category_name = "any";
        }

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

    // Step 2+3: Build EquipmentRegistry, then EnchantmentRegistry
    populate(eq_reg, ench_reg, tag_reg, equipments, enchants);
}

void RegistryLoader::resolve_with_base(
    const std::vector<business::loader::EnchantmentData>& enchants,
    const std::vector<business::loader::EquipmentData>& equipments,
    TagRegistry& tag_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg)
{
    // Equipments merge in first so their categories resolve against the
    // existing (vanilla) tag universe, then enchantments are cross-validated
    // against the union of vanilla + profile equipment and tags.
    populate(eq_reg, ench_reg, tag_reg, equipments, enchants);
}

void RegistryLoader::populate(
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg,
    const TagRegistry& tag_reg,
    const std::vector<business::loader::EquipmentData>& equipments,
    const std::vector<business::loader::EnchantmentData>& enchants)
{
    from_dto(eq_reg, tag_reg, equipments);
    from_dto(ench_reg, tag_reg, eq_reg, enchants);
}

// ============================================================================
// Full pipeline (vanilla universe → own content)
// ============================================================================

RegistryLoader::OwnContent RegistryLoader::resolve_own_content(
    const std::vector<business::loader::EnchantmentData>& enchants,
    const std::vector<business::loader::EquipmentData>& equipments,
    const TagRegistry* extra_tags)
{
    // Seed the vanilla universe into temporary registries, then cross-validate
    // the source DTOs on top of the union.  A profile must NOT keep vanilla's
    // registries as its own, so after validation we filter back to the DTOs'
    // own ids (NSID() normalization matches from_dto).
    RegistryLoader loader;
    TagRegistry tag_reg;          // vanilla universe: tags
    EquipmentRegistry eq_reg;     // vanilla universe: equipment
    EnchantmentRegistry ench_reg; // vanilla universe + source content
    besq::data::load_builtin_data(tag_reg, ench_reg, eq_reg);
    // Seed datapack-defined item tags so `#mypack:*` supported_items refs
    // resolve during from_dto (B-T14 I-1).  Brand-new tags are added; a
    // vanilla-tag override is a no-op here (TagRegistry has no member data —
    // the replace/merge semantics live in the TagResolver built downstream).
    if (extra_tags) {
        for (const auto& [id, tag] : extra_tags->data())
            tag_reg.insert(tag);
    }
    loader.resolve_with_base(enchants, equipments, tag_reg, eq_reg, ench_reg);

    std::unordered_set<NSID> ench_ids;
    for (const auto& d : enchants)
        ench_ids.insert(NSID(d.id));
    std::unordered_set<NSID> eq_ids;
    for (const auto& d : equipments)
        eq_ids.insert(NSID(d.id));

    EnchantmentRegistry own_ench;
    for (const auto& [id, info] : ench_reg.data())
        if (ench_ids.count(id) != 0)
            own_ench.insert(info);
    EquipmentRegistry own_eq;
    for (const auto& [id, eq] : eq_reg.data())
        if (eq_ids.count(id) != 0)
            own_eq.insert(eq);

    return OwnContent{std::move(own_ench), std::move(own_eq), std::move(tag_reg)};
}
