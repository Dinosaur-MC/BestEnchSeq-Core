#pragma once
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

#include "types/Platform.h"

namespace ParserUtils {

// ─── Data format ──────────────────────────────────────────────────────────
enum class DataFormat {
    Unknown,
    NativeJSON,
    NativeCSV,
    MCOfficial,
};

// ─── Platform string parsing ──────────────────────────────────────────────
inline MCE parse_platform(const std::string &str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "java" || lower == "je") return MCE::Java;
    if (lower == "bedrock" || lower == "be") return MCE::Bedrock;
    if (lower == "all" || lower == "both") return MCE::All;
    return MCE::Java;
}

inline std::string platform_to_string(MCE p) {
    switch (p) {
    case MCE::Java:    return "java";
    case MCE::Bedrock: return "bedrock";
    case MCE::All:     return "all";
    case MCE::None:    return "none";
    }
    return "java";
}

// ─── Minecraft data-pack structure detection ──────────────────────────────
inline bool is_mc_official_structure(const std::filesystem::path &dir) {
    if (!std::filesystem::is_directory(dir)) return false;
    auto data_dir = dir / "data";
    if (!std::filesystem::is_directory(data_dir)) return false;
    for (const auto &ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) continue;
        for (const auto &sub_entry : std::filesystem::directory_iterator(
                 ns_entry.path(), std::filesystem::directory_options::skip_permission_denied)) {
            if (!sub_entry.is_directory()) continue;
            std::string dirname = sub_entry.path().filename().string();
            if (dirname == "enchantment" || dirname == "tags") return true;
        }
    }
    return false;
}

// ─── File format detection ────────────────────────────────────────────────
inline DataFormat detect_format(const std::filesystem::path &path) {
    if (path.empty()) return DataFormat::Unknown;
    if (std::filesystem::is_directory(path)) {
        return is_mc_official_structure(path) ? DataFormat::MCOfficial : DataFormat::Unknown;
    }
    std::string ext = path.extension().string();
    for (auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".json") return DataFormat::NativeJSON;
    if (ext == ".csv")  return DataFormat::NativeCSV;
    return DataFormat::Unknown;
}

inline DataFormat detect_mc_official(const std::filesystem::path &directory) {
    return is_mc_official_structure(directory) ? DataFormat::MCOfficial : DataFormat::Unknown;
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
