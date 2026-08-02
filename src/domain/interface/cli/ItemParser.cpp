#include "EnchParser.h"
#include "ItemParser.h"
#include "common/i18n/Language.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "common/utils/StringUtils.hpp"
#include <cctype>
#include <limits>
#include <stdexcept>

// ============================================================================
// Helpers
// ============================================================================

/// Parse a non-negative integer from a string. Throws on failure.
static int32_t parse_nonneg_int(const std::string& str, const std::string& context) {
    if (str.empty())
        throw std::runtime_error("Empty value for " + context);
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            throw std::runtime_error(tr_fmt("cli.err.invalid_int", str, context));
    }
    // safe: all digits at this point
    unsigned long long val = std::stoull(str);
    if (val > static_cast<unsigned long long>(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("Value '" + str + "' for " + context + " exceeds int32 range");
    return static_cast<int32_t>(val);
}

/// Parse {key:value,...} properties block. Returns trailing position after '}'.
/// prior_penalty and durability are filled in if present in the block;
/// values already set are treated as defaults and are NOT overwritten
/// when the corresponding key is absent.
static size_t parse_properties(const std::string& input, size_t start,
                               int32_t& prior_penalty, int32_t& durability)
{
    if (start >= input.size() || input[start] != '{')
        return start;  // no properties block

    auto close_pos = input.find('}', start);
    if (close_pos == std::string::npos)
        throw std::runtime_error("Malformed target spec: missing '}' in '" + input + "'");

    std::string body = input.substr(start + 1, close_pos - start - 1);
    if (!body.empty()) {
        auto pairs = string_utils::split(body, ',');
        for (const auto& pair : pairs) {
            auto colon = pair.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= pair.size())
                throw std::runtime_error("Malformed property '" + pair +
                    "' (expected key:value) in '" + input + "'");

            std::string key = pair.substr(0, colon);
            std::string val = pair.substr(colon + 1);

            if (key == "prior_penalty")
                prior_penalty = parse_nonneg_int(val, "prior_penalty");
            else if (key == "durability")
                durability = parse_nonneg_int(val, "durability");
            else
                throw std::runtime_error(tr_fmt("cli.err.unknown_property", key));
        }
    }

    return close_pos + 1;  // position after '}'
}

/// Validate durability is within equipment bounds.
static void validate_durability(int32_t durability, int32_t max_durability,
                                const std::string& item_id)
{
    if (durability > max_durability)
        throw std::runtime_error("durability " + std::to_string(durability) +
            " exceeds max_durability " + std::to_string(max_durability) +
            " for '" + item_id + "'");
}

/// Validate prior_penalty fits the compact representation (uint8_t).
/// Larger values would silently wrap (e.g. 999 → 231) downstream.
static void validate_prior_penalty(int32_t prior_penalty)
{
    if (prior_penalty > std::numeric_limits<uint8_t>::max())
        throw std::runtime_error(
            tr_fmt("cli.err.prior_penalty_exceeds_max",
                   prior_penalty, std::numeric_limits<uint8_t>::max()));
}

// ============================================================================
// ItemParser::parse
// ============================================================================

Item ItemParser::parse(const std::string &input,
                       const EnchantmentRegistry &ench_reg,
                       const EquipmentRegistry &eq_reg)
{
    std::string item_id;
    EnchSet ench_set;
    int32_t prior_penalty = 0;
    int32_t durability = 0;
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

    // ── Look up equipment (needed for max_durability defaults) ──
    // NSID() throws its bare validator text ("The NSID '...' is invalid") when
    // the id contains now-invalid chars (uppercase, `/` in ns, `.`/`..`
    // segments).  Such input is genuinely unknown/invalid — map it to the
    // actionable unknown-equipment error instead (B-T24 #22).
    auto make_nsid = [](const std::string& k) -> NSID {
        try {
            return NSID(k);
        } catch (const std::exception&) {
            throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", k));
        }
    };
    auto eq_it = eq_reg.find(make_nsid(item_id));
    if (eq_it == eq_reg.end())
        throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", item_id));

    // ── Set defaults from equipment data ──
    durability = eq_it->max_durability;

    // ── Parse properties block { ... } (overrides defaults) ──
    size_t after_props = parse_properties(input, pos, prior_penalty, durability);

    // ── Reject trailing content ──
    auto trailing = input.find_first_not_of(" \t", after_props);
    if (trailing != std::string::npos)
        throw std::runtime_error(
            "Malformed target spec: unexpected content after '}' in '" + input + "'");

    // ── Post-parse validation ──
    validate_durability(durability, eq_it->max_durability, item_id);
    validate_prior_penalty(prior_penalty);

    return Item(eq_it->id, ench_set, prior_penalty, durability);
}
