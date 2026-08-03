#include "InventoryParser.h"
#include "common/i18n/Language.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "InventorySchema.h"

#include <cstdint>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

/// Construct an NSID, mapping NSID's bare validator text ("The NSID '...' is
/// invalid") for now-invalid input (uppercase, `/` in ns, `.`/`..` segments)
/// to the actionable registry error instead.
NSID make_nsid(const std::string& k, const char* err_key) {
    try {
        return NSID(k);
    } catch (const std::exception&) {
        throw std::runtime_error(tr_fmt(err_key, k));
    }
}

/// Validate prior_penalty fits the compact representation (uint8_t).
/// Negative or >255 values would silently wrap (e.g. -5→251, 999→231)
/// downstream in CompactAdapter.
static void validate_prior_penalty(int32_t prior_penalty) {
    if (prior_penalty < 0)
        throw std::runtime_error(tr_fmt("cli.err.prior_penalty_negative", prior_penalty));
    if (prior_penalty > std::numeric_limits<uint8_t>::max())
        throw std::runtime_error(
            tr_fmt("cli.err.prior_penalty_exceeds_max", prior_penalty, std::numeric_limits<uint8_t>::max()));
}

/// Validate durability is within equipment bounds.
static void validate_durability(int32_t durability, int32_t max_durability, const std::string& item_id) {
    if (durability > max_durability)
        throw std::runtime_error(tr_fmt("cli.err.durability_exceeds_max", durability, max_durability, item_id));
}

/// Build an EnchSet from schema DTOs.  Throws on empty/unknown ids,
/// out-of-range/over-max levels, or duplicate enchantments.
EnchSet parse_ench_array(const std::vector<InvEnchDto>& enchs, const EnchantmentRegistry& ench_reg) {
    EnchSet result;
    for (const auto& e : enchs) {
        if (e.id.empty())
            throw std::runtime_error(tr_fmt("cli.err.empty_ench_id", e.id));
        if (e.level < 1 || e.level > 255)
            throw std::runtime_error(tr_fmt("cli.err.invalid_ench_level", e.level, e.id));
        auto it = ench_reg.find(make_nsid(e.id, "cli.err.unknown_ench"));
        if (it == ench_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_ench", e.id));
        if (e.level > it->max_level)
            throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max", it->id.str(), e.level, it->max_level));
        // Duplicate IDs (regardless of level) are invalid — an item has at
        // most one level per enchantment.  Reject instead of silently keeping
        // the first entry (consistent with EnchParser).
        if (result.find(it->id) != result.end())
            throw std::runtime_error(tr_fmt("cli.err.duplicate_ench", it->id.str()));
        result.emplace(it->id, it->name, e.level);
    }
    return result;
}

/// Build the target Item from a schema DTO (aligns with ItemParser: books and
/// enchanted_books normalise to the enchanted_book item with no durability;
/// anything else must be known equipment, starting clean at full durability).
Item build_target_item(const InvTargetDto& target, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg) {
    EnchSet ench_set = parse_ench_array(target.enchants, ench_reg);

    NSID nid = make_nsid(target.item, "cli.err.unknown_equipment");
    if (nid == NSID("minecraft:book") || nid == NSID("minecraft:enchanted_book"))
        return Item(NSID("minecraft:enchanted_book"), ench_set, 0, 0);

    auto eq_it = eq_reg.find(nid);
    if (eq_it == eq_reg.end())
        throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", target.item));
    return Item(eq_it->id, ench_set, 0, eq_it->max_durability);
}

} // namespace

InvTaskDto InventoryParser::parse_task(const std::string& content) {
    Json root;
    try {
        root = Json::parse(content);
    } catch (const std::exception& e) {
        throw std::runtime_error(tr_fmt("cli.err.inventory_file_error", e.what()));
    }

    InvTaskDto dto;
    ds::ErrorList err;
    if (!InvTaskJson::parse(root, dto, err))
        throw std::runtime_error(tr_fmt("cli.err.inventory_schema_error", err.str()));
    return dto;
}

InventoryInput InventoryParser::build_inventory(const InvTaskDto& dto,
                                                const EnchantmentRegistry& ench_reg,
                                                const EquipmentRegistry& eq_reg) {
    InventoryInput out;

    // ── target ──
    if (!dto.target.item.empty())
        out.target_item = build_target_item(dto.target, ench_reg, eq_reg);

    // ── items ──
    for (const auto& it : dto.items) {
        if (it.type != "book" && it.type != "equipment")
            throw std::runtime_error(tr_fmt("cli.err.inventory_bad_type", it.type));

        validate_prior_penalty(it.prior_penalty);
        EnchSet ench_set = parse_ench_array(it.enchants, ench_reg);

        if (it.type == "book") {
            out.items.emplace_back(NSID("minecraft:enchanted_book"), ench_set, it.prior_penalty);
        } else {
            if (it.id.empty())
                throw std::runtime_error(tr("cli.err.inventory_missing_id"));
            auto eq_it = eq_reg.find(make_nsid(it.id, "cli.err.unknown_equipment"));
            if (eq_it == eq_reg.end())
                throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", it.id));
            int32_t dur = eq_it->max_durability;
            if (it.durability > 0)
                dur = it.durability;
            validate_durability(dur, eq_it->max_durability, it.id);
            out.items.emplace_back(eq_it->id, ench_set, it.prior_penalty, dur);
        }
        out.priorities.push_back(it.priority);
    }

    out.algorithm = dto.algorithm;
    out.profile = dto.profile;
    return out;
}

InventoryInput InventoryParser::parse_string(const std::string& content,
                                             const EnchantmentRegistry& ench_reg,
                                             const EquipmentRegistry& eq_reg) {
    return build_inventory(parse_task(content), ench_reg, eq_reg);
}

std::string InventoryParser::read_content(const std::string& path) {
    if (path == "-")
        return std::string((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

    try {
        return file_utils::read_file(path);
    } catch (const std::exception& e) {
        throw std::runtime_error(tr_fmt("cli.err.inventory_file_error", e.what()));
    }
}

InventoryInput
InventoryParser::parse_file(const std::string& path, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg) {
    return parse_string(read_content(path), ench_reg, eq_reg);
}
