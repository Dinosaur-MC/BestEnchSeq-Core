#pragma once
#include <cctype>
#include <string>
#include <utility>

#include "common/CommonTypes.h"

// File-format detection (DataFormat, detect_format, …)
// moved to domain/interface/fs/FileFormat.h
#include "domain/interface/fs/FileFormat.h"  // IWYU pragma: export

namespace ParserUtils {

// ─── Platform string parsing ──────────────────────────────────────────────
inline MCE parse_platform(const std::string &str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "java" || lower == "je") return MCE::Java;
    if (lower == "bedrock" || lower == "be") return MCE::Bedrock;
    return MCE::Java;
}

inline std::string platform_to_string(MCE p) {
    switch (p) {
    case MCE::Java:    return "java";
    case MCE::Bedrock: return "bedrock";
    default:                     return "unknown";
    }
}

// ─── Namespace helpers ────────────────────────────────────────────────────
inline std::pair<std::string, std::string> split_namespace(const std::string &qualified_id) {
    size_t colon_pos = qualified_id.find(':');
    if (colon_pos == std::string::npos) return {std::string(), qualified_id};
    return {qualified_id.substr(0, colon_pos), qualified_id.substr(colon_pos + 1)};
}

inline std::string qualify_id(const std::string &id, const std::string &default_ns = "minecraft") {
    if (id.find(':') != std::string::npos) return id;
    return default_ns + ":" + id;
}

} // namespace ParserUtils
