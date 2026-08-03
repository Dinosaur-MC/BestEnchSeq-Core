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
#include <stdexcept>
#include <utility>

namespace {

/// Build an EnchSet from schema DTOs.  Throws on empty/unknown ids or
/// out-of-range/over-max levels.
EnchSet parse_ench_array(const std::vector<InvEnchDto>& enchs, const EnchantmentRegistry& ench_reg) {
    EnchSet result;
    for (const auto& e : enchs) {
        if (e.id.empty())
            throw std::runtime_error(tr_fmt("cli.err.empty_ench_id", e.id));
        if (e.level < 1 || e.level > 255)
            throw std::runtime_error(tr_fmt("cli.err.invalid_ench_level", e.level, e.id));
        auto it = ench_reg.find(NSID(e.id));
        if (it == ench_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_ench", e.id));
        if (e.level > it->max_level)
            throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max", it->id.str(), e.level, it->max_level));
        result.emplace(it->id, it->name, e.level);
    }
    return result;
}

/// Build the target Item from a schema DTO (aligns with ItemParser: books and
/// enchanted_books normalise to the enchanted_book item with no durability;
/// anything else must be known equipment, starting clean at full durability).
Item build_target_item(const InvTargetDto& target, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg) {
    EnchSet ench_set = parse_ench_array(target.enchants, ench_reg);

    // NSID() throws its bare validator text on now-invalid chars — map it to
    // the actionable unknown-equipment error instead (mirrors ItemParser).
    auto make_nsid = [](const std::string& k) -> NSID {
        try {
            return NSID(k);
        } catch (const std::exception&) {
            throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", k));
        }
    };
    NSID nid = make_nsid(target.item);
    if (nid == NSID("minecraft:book") || nid == NSID("minecraft:enchanted_book"))
        return Item(NSID("minecraft:enchanted_book"), ench_set, 0, 0);

    auto eq_it = eq_reg.find(nid);
    if (eq_it == eq_reg.end())
        throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", target.item));
    return Item(eq_it->id, ench_set, 0, eq_it->max_durability);
}

} // namespace

InventoryInput InventoryParser::parse_string(const std::string& content,
                                             const EnchantmentRegistry& ench_reg,
                                             const EquipmentRegistry& eq_reg) {
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

    InventoryInput out;

    // ── target ──
    if (!dto.target.item.empty())
        out.target_item = build_target_item(dto.target, ench_reg, eq_reg);

    // ── items ──
    for (const auto& it : dto.items) {
        if (it.type != "book" && it.type != "equipment")
            throw std::runtime_error(tr_fmt("cli.err.inventory_bad_type", it.type));

        EnchSet ench_set = parse_ench_array(it.enchants, ench_reg);

        if (it.type == "book") {
            out.items.emplace_back(NSID("minecraft:enchanted_book"), ench_set, it.prior_penalty);
        } else {
            if (it.id.empty())
                throw std::runtime_error(tr("cli.err.inventory_missing_id"));
            auto eq_it = eq_reg.find(NSID(it.id));
            if (eq_it == eq_reg.end())
                throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", it.id));
            int32_t dur = eq_it->max_durability;
            if (it.durability > 0)
                dur = it.durability;
            if (dur > eq_it->max_durability)
                throw std::runtime_error("durability " + std::to_string(dur) + " exceeds max_durability " +
                                         std::to_string(eq_it->max_durability) + " for '" + it.id + "'");
            out.items.emplace_back(eq_it->id, ench_set, it.prior_penalty, dur);
        }
        out.priorities.push_back(it.priority);
    }

    out.algorithm = std::move(dto.algorithm);
    out.profile = std::move(dto.profile);
    return out;
}

InventoryInput
InventoryParser::parse_file(const std::string& path, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg) {
    if (path == "-") {
        std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        return parse_string(content, ench_reg, eq_reg);
    }

    std::string content;
    try {
        content = file_utils::read_file(path);
    } catch (const std::exception& e) {
        throw std::runtime_error(tr_fmt("cli.err.inventory_file_error", e.what()));
    }
    return parse_string(content, ench_reg, eq_reg);
}
