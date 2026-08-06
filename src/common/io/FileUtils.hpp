#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace file_utils {

constexpr size_t MAX_FILE_SIZE = 64 * 1024 * 1024;  // 64 MiB

inline std::string read_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Not a regular file: " + path.string());

    auto file_size = std::filesystem::file_size(path);
    if (file_size > MAX_FILE_SIZE)
        throw std::runtime_error("File too large (" + std::to_string(file_size) + " bytes, max " + std::to_string(MAX_FILE_SIZE) + "): " + path.string());

    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::vector<std::filesystem::path> find_files(
    const std::filesystem::path& dir, const std::string& extension)
{
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::is_directory(dir)) return result;
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (ext.empty() || entry.path().extension() == ext)
                result.push_back(entry.path());
        }
    }
    return result;
}

/// One non-recursive directory entry (powers the web directory picker).
/// `size` is meaningful for regular files only (0 for dirs / unreadable).
struct DirEntry {
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
};

/// List the DIRECT children of `dir` (no recursion). Directories sort first,
/// then by name — the picker's natural order. A non-directory / unreadable
/// path yields an empty list (never throws).
inline std::vector<DirEntry> list_directory(const std::filesystem::path& dir) {
    std::vector<DirEntry> result;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return result;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        DirEntry e;
        e.name = entry.path().filename().string();
        std::error_code ec2;
        e.is_dir = entry.is_directory(ec2);
        if (!e.is_dir && entry.is_regular_file(ec2))
            e.size = entry.file_size(ec2);   // 0 when unreadable — fine for a picker
        result.push_back(std::move(e));
    }
    std::sort(result.begin(), result.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;   // directories first
        return a.name < b.name;
    });
    return result;
}

} // namespace file_utils
