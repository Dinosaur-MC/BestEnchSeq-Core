#pragma once

#include "common/CommonTypes.h"
#include "common/i18n/Language.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

// ============================================================================
// Internal helpers shared across interface parsers (not part of public API)
// ============================================================================

namespace interface_detail {

// ── NSID builder ───────────────────────────────────────────────────────
// Construct an NSID, mapping NSID's bare validator text ("The NSID '...' is
// invalid") for now-invalid input (uppercase, `/` in ns, `.`/`..` segments)
// to the actionable registry error (`err_key`) instead.

inline NSID make_nsid(const std::string& k, const char* err_key) {
    try {
        return NSID(k);
    } catch (const std::exception&) {
        throw std::runtime_error(tr_fmt(err_key, k));
    }
}

// ── prior_penalty validation ───────────────────────────────────────────
// Validate prior_penalty fits the compact representation (uint8_t).
// Negative or >255 values would silently wrap (e.g. -5→251, 999→231)
// downstream in CompactAdapter.

inline void validate_prior_penalty(int32_t prior_penalty) {
    if (prior_penalty < 0)
        throw std::runtime_error(tr_fmt("cli.err.prior_penalty_negative", prior_penalty));
    if (prior_penalty > std::numeric_limits<uint8_t>::max())
        throw std::runtime_error(
            tr_fmt("cli.err.prior_penalty_exceeds_max", prior_penalty, std::numeric_limits<uint8_t>::max()));
}

// ── durability validation ──────────────────────────────────────────────
// Validate durability is within equipment bounds.

inline void validate_durability(int32_t durability, int32_t max_durability, const std::string& item_id) {
    if (durability > max_durability)
        throw std::runtime_error(tr_fmt("cli.err.durability_exceeds_max", durability, max_durability, item_id));
}

// ── non-negative integer parsing ───────────────────────────────────────
// Parse a non-negative integer from a string. Throws on failure.

inline int32_t parse_nonneg_int(const std::string& str, const std::string& context) {
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

} // namespace interface_detail
