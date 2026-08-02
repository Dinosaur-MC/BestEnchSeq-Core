#pragma once

#include "domain/business/components/TagResolver.h"
#include "builtin/ItemProperties.h"
#include "common/CommonTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================================
// Internal helpers shared across format parsers (not part of public API)
// ============================================================================

namespace business::parser_detail {

// ── NSID builder ───────────────────────────────────────────────────────
// Build a qualified NSID from a bare or namespaced string.

inline NSID make_id(const std::string& id_str, const std::string& default_ns = "minecraft") {
    if (id_str.find(':') != std::string::npos)
        return NSID(id_str);
    return NSID(default_ns, id_str);
}

// ── Reference resolution ───────────────────────────────────────────────
// Resolve a list of mixed concrete IDs and #tag references via TagResolver.
// This expansion helper is used only for `exclusive_set`. In contrast,
// `supported_items` references are passed through
// RAW (not expanded) by the parsers (T5); the loader performs
// cross-validation against them (T6).

inline std::unordered_set<std::string>
resolve_references(const std::vector<std::string>& items, TagResolver& tag_resolver) {
    std::unordered_set<std::string> result;
    for (const auto& item : items) {
        auto expanded = tag_resolver.resolve(item);
        result.insert(expanded.begin(), expanded.end());
    }
    return result;
}

// ── Display name derivation ────────────────────────────────────────────

inline std::string derive_display_name(const std::string& item_id) {
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos) {
        key = key.substr(colon + 1);
    }

    if (!key.empty()) {
        key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    }
    for (auto& c : key) {
        if (c == '_') {
            c = ' ';
        }
    }
    return key;
}

/// Derive the display short name of a category NSID (e.g. `#minecraft:sword`
/// → "sword").  Under the real-MC-tag model (T10) an equipment's category is a
/// display-only label that may NOT be a *defined* tag, so serializers fall
/// back to this short form instead of emitting "unknown"/"any".
inline std::string category_short_name(const NSID& category) {
    std::string s = category.str();
    if (!s.empty() && s[0] == '#')
        s = s.substr(1);
    auto colon = s.find(':');
    if (colon != std::string::npos)
        s = s.substr(colon + 1);
    return s;
}

// ── Category derivation by suffix ──────────────────────────────────────

// Returns the equipment category derived from an item id's suffix.
// Defined in ParserShared.cpp (moved out of header to avoid PIC issues
// with function-local static std::unordered_map).
std::string get_category_suffix(const std::string& item_id);

} // namespace business::parser_detail
