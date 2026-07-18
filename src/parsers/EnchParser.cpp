#include "parsers/EnchParser.h"
#include "utils/ParserUtils.hpp"
#include "log/log.hpp"

#include <cctype>
#include <stdexcept>

// ─── helpers ──────────────────────────────────────────────────────────────────

static bool is_all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// ============================================================================
// EnchParser::parse — parse enchantment spec strings
// ============================================================================

std::vector<EnchantmentSpec> EnchParser::parse(const std::string& input) {
    std::vector<EnchantmentSpec> result;
    auto tokens = ParserUtils::split_string(input, ',');

    for (const auto& token : tokens) {
        EnchantmentSpec spec;

        // ── Look for '=' separator (level) ─────────────────────────────────
        auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            // Level from after '='
            std::string level_str = token.substr(eq_pos + 1);
            try {
                spec.level = std::stoi(level_str);
            } catch (const std::exception&) {
                throw std::runtime_error(
                    "Invalid enchantment level: '" + level_str + "' in '" + token + "'");
            }
            if (spec.level < 1 || spec.level > 255) {
                throw std::runtime_error(
                    "Invalid enchantment level: '" + level_str + "' in '" + token + "'");
            }

            // Spec part before '='
            std::string spec_part = token.substr(0, eq_pos);
            if (spec_part.empty()) {
                throw std::runtime_error("Empty enchantment id in '" + token + "'");
            }

            auto colon_pos = spec_part.find(':');
            if (colon_pos != std::string::npos) {
                spec.ns = spec_part.substr(0, colon_pos);
                spec.id = spec_part.substr(colon_pos + 1);
            } else {
                spec.ns = "minecraft";
                spec.id = spec_part;
            }
        } else {
            // ── No '=' — check for colon shorthand or namespace prefix ────
            auto colon_pos = token.find(':');
            if (colon_pos != std::string::npos) {
                std::string after = token.substr(colon_pos + 1);
                if (is_all_digits(after)) {
                    // Colon shorthand: id:level
                    spec.ns = "minecraft";
                    spec.id = token.substr(0, colon_pos);
                    try {
                        spec.level = std::stoi(after);
                    } catch (const std::exception&) {
                        throw std::runtime_error(
                            "Invalid enchantment level: '" + after + "' in '" + token + "'");
                    }
                    if (spec.level < 1 || spec.level > 255) {
                        throw std::runtime_error(
                            "Invalid enchantment level: '" + after + "' in '" + token + "'");
                    }
                } else {
                    // Namespace prefix: ns:id
                    spec.ns = token.substr(0, colon_pos);
                    spec.id = after;
                    spec.level = 1;
                }
            } else {
                // Plain id, no namespace, no level
                spec.ns = "minecraft";
                spec.id = token;
                spec.level = 1;
            }
        }

        // ── Validate id is not empty ──────────────────────────────────────
        if (spec.id.empty()) {
            throw std::runtime_error("Empty enchantment id in '" + token + "'");
        }

        result.push_back(std::move(spec));
    }

    return result;
}
