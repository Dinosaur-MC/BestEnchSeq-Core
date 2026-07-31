#include "InventoryParser.h"
#include "common/i18n/Language.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"

#include <cstdint>

namespace {

/// Parse an enchantment array [{ "id": "...", "level": N }, ...] into an
/// EnchSet.  Throws on empty/unknown ids or out-of-range levels.
EnchSet parse_ench_array(const Json::Array &arr,
                         const EnchantmentRegistry &ench_reg) {
    EnchSet result;
    for (const auto &elem : arr) {
        auto obj = elem.as<Json::Object>();
        std::string eid;
        if (auto it = obj.find("id"); it != obj.end())
            eid = it->second.as<std::string>();
        int64_t level = 1;
        if (auto it = obj.find("level"); it != obj.end())
            level = it->second.as<int64_t>();
        if (eid.empty())
            throw std::runtime_error(tr_fmt("cli.err.empty_ench_id", eid));
        if (level < 1 || level > 255)
            throw std::runtime_error(tr_fmt("cli.err.invalid_ench_level",
                                            level, eid));
        auto it = ench_reg.find(NSID(eid));
        if (it == ench_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_ench", eid));
        result.emplace(it->id, it->name, static_cast<int32_t>(level));
    }
    return result;
}

} // namespace

InventoryInput InventoryParser::parse_file(const std::string &path,
                                           const EnchantmentRegistry &ench_reg,
                                           const EquipmentRegistry &eq_reg) {
    std::string content;
    try {
        content = file_utils::read_file(path);
    } catch (const std::exception &e) {
        throw std::runtime_error(tr_fmt("cli.err.inventory_file_error", e.what()));
    }

    Json root;
    try {
        root = Json::parse(content);
    } catch (const std::exception &e) {
        throw std::runtime_error(tr_fmt("cli.err.inventory_file_error", e.what()));
    }

    auto root_obj = root.as<Json::Object>();
    InventoryInput out;
    auto items_it = root_obj.find("items");
    if (items_it == root_obj.end())
        return out;  // no items → empty inventory

    for (const auto &elem : items_it->second.as_array()) {
        auto obj = elem.as<Json::Object>();
        std::string type;
        if (auto it = obj.find("type"); it != obj.end())
            type = it->second.as<std::string>();
        if (type != "book" && type != "equipment")
            throw std::runtime_error(tr_fmt("cli.err.inventory_bad_type", type));

        // ── enchantments ──
        EnchSet ench_set;
        if (auto it = obj.find("enchants"); it != obj.end())
            ench_set = parse_ench_array(it->second.as_array(), ench_reg);

        // ── prior_penalty ──
        int32_t ppn = 0;
        if (auto it = obj.find("prior_penalty"); it != obj.end())
            ppn = static_cast<int32_t>(it->second.as<int64_t>());

        // ── priority (lower = preferred as sacrifice) ──
        int32_t priority = 99;
        if (auto it = obj.find("priority"); it != obj.end())
            priority = static_cast<int32_t>(it->second.as<int64_t>());

        if (type == "book") {
            out.items.emplace_back(NSID("minecraft:enchanted_book"),
                                   ench_set, ppn);
        } else {
            std::string eid;
            if (auto it = obj.find("id"); it != obj.end())
                eid = it->second.as<std::string>();
            if (eid.empty())
                throw std::runtime_error(tr("cli.err.inventory_missing_id"));
            auto eq_it = eq_reg.find(NSID(eid));
            if (eq_it == eq_reg.end())
                throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", eid));
            int32_t dur = eq_it->max_durability;
            if (auto it = obj.find("durability"); it != obj.end()) {
                int64_t v = it->second.as<int64_t>();
                if (v > 0)
                    dur = static_cast<int32_t>(v);
            }
            if (dur > eq_it->max_durability)
                throw std::runtime_error("durability " + std::to_string(dur) +
                    " exceeds max_durability " +
                    std::to_string(eq_it->max_durability) + " for '" + eid + "'");
            out.items.emplace_back(eq_it->id, ench_set, ppn, dur);
        }
        out.priorities.push_back(priority);
    }

    return out;
}
