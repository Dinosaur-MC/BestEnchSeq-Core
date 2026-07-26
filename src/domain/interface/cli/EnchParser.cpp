#include "EnchParser.h"
#include "common/i18n/Language.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "common/utils/StringUtils.hpp"

#include <cctype>
#include <stdexcept>

// ─── helpers ──────────────────────────────────────────────────────────────────

/// Check if string is a valid integer (optional leading +/- then digits).
static bool is_integer(const std::string& s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '+' || s[0] == '-') {
        if (s.size() == 1) return false;  // just a sign
        start = 1;
    }
    for (size_t i = start; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

// ============================================================================
// EnchParser::parse — parse enchantment spec strings into an EnchSet
// ============================================================================

EnchSet EnchParser::parse(const std::string& input,
                          const EnchantmentRegistry& ench_reg)
{
    EnchSet result;

    // Check for empty tokens (e.g. "a=1,,b=2") before splitting
    for (size_t i = 0; i + 1 < input.size(); ++i) {
        if (input[i] == ',' && input[i + 1] == ',') {
            throw std::runtime_error("Empty enchantment spec in list (double comma near position " + std::to_string(i) + ")");
        }
    }
    if (!input.empty() && input.back() == ',') {
        throw std::runtime_error("Trailing comma in enchantment list");
    }

    auto tokens = string_utils::split(input, ',');

    for (const auto& token : tokens) {
        std::string ns, id;
        int level = 1;

        // ── Look for '=' separator (level) ─────────────────────────────────
        auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            // Level from after '='
            std::string level_str = token.substr(eq_pos + 1);
            try {
                level = std::stoi(level_str);
            } catch (const std::exception&) {
                throw std::runtime_error(
                    tr_fmt("cli.err.invalid_ench_level", level_str, token));
            }
            if (level < 1 || level > 255) {
                throw std::runtime_error(
                    tr_fmt("cli.err.invalid_ench_level", level_str, token));
            }

            // Spec part before '='
            std::string spec_part = token.substr(0, eq_pos);
            if (spec_part.empty()) {
                throw std::runtime_error("Empty enchantment id in '" + token + "'");
            }

            auto colon_pos = spec_part.find(':');
            if (colon_pos != std::string::npos) {
                ns = spec_part.substr(0, colon_pos);
                id = spec_part.substr(colon_pos + 1);
            } else {
                ns = "minecraft";
                id = spec_part;
            }
        } else {
            // ── No '=' — check for colon shorthand or namespace prefix ────
            auto colon_pos = token.find(':');
            if (colon_pos != std::string::npos) {
                std::string after = token.substr(colon_pos + 1);
                if (is_integer(after)) {
                    // Colon shorthand: id:level
                    ns = "minecraft";
                    id = token.substr(0, colon_pos);
                    try {
                        level = std::stoi(after);
                    } catch (const std::exception&) {
                        throw std::runtime_error(
                            "Invalid enchantment level: '" + after + "' in '" + token + "'");
                    }
                    if (level < 1 || level > 255) {
                        throw std::runtime_error(
                            "Invalid enchantment level: '" + after + "' in '" + token + "'");
                    }
                } else {
                    // Namespace prefix: ns:id
                    ns = token.substr(0, colon_pos);
                    id = after;
                    level = 1;
                }
            } else {
                // Plain id, no namespace, no level
                ns = "minecraft";
                id = token;
                level = 1;
            }
        }

        // ── Validate id is not empty ──────────────────────────────────────
        if (id.empty()) {
            throw std::runtime_error("Empty enchantment id in '" + token + "'");
        }

        // ── Resolve against registry and insert into EnchSet ──────────────
        std::string key = (ns.empty() || ns == "minecraft") ? id : ns + ":" + id;
        auto it = ench_reg.find(NSID(key));
        if (it == ench_reg.end()) {
            // bare-ID fallback
            it = ench_reg.find(NSID(id));
            if (it == ench_reg.end())
                throw std::runtime_error(tr_fmt("cli.err.unknown_ench", key));
        }
        result.emplace(it->id, it->name, level);
    }

    return result;
}
