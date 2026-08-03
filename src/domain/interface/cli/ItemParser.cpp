#include "ItemParser.h"
#include "common/i18n/Language.h"
#include "common/utils/StringUtils.hpp"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/interface/components/ParserShared.hpp"
#include "EnchParser.h"
#include <stdexcept>

// ============================================================================
// Helpers
// ============================================================================

/// Parse {key:value,...} properties block. Returns trailing position after '}'.
/// prior_penalty and durability are filled in if present in the block;
/// values already set are treated as defaults and are NOT overwritten
/// when the corresponding key is absent.
static size_t parse_properties(const std::string& input, size_t start, int32_t& prior_penalty, int32_t& durability) {
    if (start >= input.size() || input[start] != '{')
        return start; // no properties block

    auto close_pos = input.find('}', start);
    if (close_pos == std::string::npos)
        throw std::runtime_error("Malformed target spec: missing '}' in '" + input + "'");

    std::string body = input.substr(start + 1, close_pos - start - 1);
    if (!body.empty()) {
        auto pairs = string_utils::split(body, ',');
        for (const auto& pair : pairs) {
            auto colon = pair.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= pair.size())
                throw std::runtime_error("Malformed property '" + pair + "' (expected key:value) in '" + input + "'");

            std::string key = pair.substr(0, colon);
            std::string val = pair.substr(colon + 1);

            if (key == "prior_penalty")
                prior_penalty = interface_detail::parse_nonneg_int(val, "prior_penalty");
            else if (key == "durability")
                durability = interface_detail::parse_nonneg_int(val, "durability");
            else
                throw std::runtime_error(tr_fmt("cli.err.unknown_property", key));
        }
    }

    return close_pos + 1; // position after '}'
}

// ============================================================================
// ItemParser::parse
// ============================================================================

Item ItemParser::parse(const std::string& input, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg) {
    std::string item_id;
    EnchSet ench_set;
    int32_t prior_penalty = 0;
    int32_t durability = 0;
    int32_t max_durability = 0;
    size_t pos = 0;

    // ── Parse enchantment block [ ... ] ──
    auto bracket_pos = input.find('[', pos);
    if (bracket_pos != std::string::npos) {
        auto close_pos = input.find(']', bracket_pos);
        if (close_pos == std::string::npos)
            throw std::runtime_error("Malformed target spec: missing ']' in '" + input + "'");

        item_id = input.substr(pos, bracket_pos - pos);
        std::string inline_str = input.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
        if (!inline_str.empty())
            ench_set = EnchParser::parse(inline_str, ench_reg);

        pos = close_pos + 1;
    } else {
        // No brackets — everything up to '{' or end is the item id
        auto brace_pos = input.find('{', pos);
        item_id = input.substr(pos, (brace_pos == std::string::npos ? input.size() : brace_pos) - pos);
        pos = brace_pos == std::string::npos ? input.size() : brace_pos;
    }

    if (item_id.empty())
        throw std::runtime_error("Empty item id in target spec");

    // ── Resolve item type ──
    // Books are valid forge targets but are not equipment: a plain `book` has
    // no durability and cannot hold enchantments itself — enchanting it turns
    // it into an `enchanted_book`.  Both ids therefore normalise to the
    // enchanted_book (the item that actually carries the enchantments), and
    // durability defaults to 0 (books have no durability).
    NSID nid = interface_detail::make_nsid(item_id, "cli.err.unknown_equipment");
    const bool is_book = (nid == NSID("minecraft:book") || nid == NSID("minecraft:enchanted_book"));
    if (is_book) {
        item_id = "enchanted_book"; // book → enchanted_book on enchanting
        durability = 0;
        max_durability = 0;
    } else {
        auto eq_it = eq_reg.find(nid);
        if (eq_it == eq_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", item_id));
        durability = eq_it->max_durability;
        max_durability = eq_it->max_durability;
    }

    // ── Parse properties block { ... } (overrides defaults) ──
    size_t after_props = parse_properties(input, pos, prior_penalty, durability);

    // ── Reject trailing content ──
    auto trailing = input.find_first_not_of(" \t", after_props);
    if (trailing != std::string::npos)
        throw std::runtime_error("Malformed target spec: unexpected content after '}' in '" + input + "'");

    // ── Post-parse validation ──
    interface_detail::validate_durability(durability, max_durability, item_id);
    interface_detail::validate_prior_penalty(prior_penalty);

    return Item(NSID(item_id), ench_set, prior_penalty, durability);
}
