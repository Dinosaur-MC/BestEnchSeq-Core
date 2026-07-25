#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/io/json.h"

namespace ParserUtils {

// ─── JSON field extraction helpers ──────────────────────────────────────────
inline std::string get_json_string(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) return {};
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::String>(val))
        return std::get<Json::String>(val);
    return {};
}

inline int32_t get_json_int(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) return 0;
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::Number>(val)) {
        const auto &num = std::get<Json::Number>(val);
        if (std::holds_alternative<int64_t>(num)) return static_cast<int32_t>(std::get<int64_t>(num));
        if (std::holds_alternative<double>(num))  return static_cast<int32_t>(std::get<double>(num));
    }
    return 0;
}

inline bool get_json_bool(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) return false;
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::Bool>(val))
        return std::get<Json::Bool>(val);
    return false;
}

inline std::vector<std::string> get_json_string_array(const Json::Object &obj, const std::string &key) {
    std::vector<std::string> result;
    auto it = obj.find(key);
    if (it == obj.end()) return result;
    auto val = it->second.get_value();
    if (!std::holds_alternative<Json::Array>(val)) return result;
    const auto &arr = std::get<Json::Array>(val);
    result.reserve(arr.size());
    for (const auto &elem : arr) {
        auto elem_val = elem.get_value();
        if (std::holds_alternative<Json::String>(elem_val))
            result.push_back(std::get<Json::String>(elem_val));
    }
    return result;
}

// ─── String splitting ──────────────────────────────────────────────────────
inline std::vector<std::string> split_string(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    if (str.empty()) return tokens;
    size_t start = 0;
    while (true) {
        size_t end = str.find(delimiter, start);
        if (end == std::string::npos) {
            if (start < str.size()) tokens.push_back(str.substr(start));
            break;
        }
        if (end > start) tokens.push_back(str.substr(start, end - start));
        start = end + 1;
    }
    return tokens;
}

// ─── File I/O ───────────────────────────────────────────────────────────────

inline constexpr size_t MAX_FILE_SIZE = 64 * 1024 * 1024;  // 64 MiB

inline std::string read_file(const std::filesystem::path &path) {
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
    const std::filesystem::path &dir, const std::string &extension
) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::is_directory(dir)) return result;
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (ext.empty() || entry.path().extension() == ext)
                result.push_back(entry.path());
        }
    }
    return result;
}

} // namespace ParserUtils
