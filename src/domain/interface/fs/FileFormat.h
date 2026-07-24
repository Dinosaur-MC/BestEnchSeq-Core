#pragma once
#include <filesystem>
#include <string>

// ====================================================================
// BestEnchSeq — Interface Domain: File Format Detection
// ====================================================================
// Data format identification for the besq data-file ecosystem.
// Detects Native JSON (all-in-one), Native CSV, and MC Official
// data-pack structure formats.
//
// All symbols live in the ParserUtils namespace for backward
// compatibility with existing call sites.

namespace ParserUtils {

/// Supported data file formats.
enum class DataFormat {
    Unknown,
    NativeJSON,   ///< All-in-one JSON (data/vanilla.json)
    NativeCSV,    ///< CSV format (data/*.csv)
    MCOfficial,   ///< Minecraft data-pack structure (data/<ns>/...)
};

/// Check whether a directory follows the MC official data-pack layout.
/// Looks for a `data/` subdirectory containing at least one namespace
/// directory with an `enchantment/` or `tags/` subdirectory.
[[nodiscard]] inline bool is_mc_official_structure(const std::filesystem::path &dir) {
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

/// Detect the data format from a file path or directory.
///
/// For files: checks extension (.json / .csv).
/// For directories: checks whether it follows the MC official structure.
[[nodiscard]] inline DataFormat detect_format(const std::filesystem::path &path) {
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

/// Explicit MC official structure check.
[[nodiscard]] inline DataFormat detect_mc_official(const std::filesystem::path &directory) {
    return is_mc_official_structure(directory) ? DataFormat::MCOfficial : DataFormat::Unknown;
}

} // namespace ParserUtils
