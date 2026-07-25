#pragma once
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

} // namespace file_utils
