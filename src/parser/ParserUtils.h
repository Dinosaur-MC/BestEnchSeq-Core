#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "types/common.h"

namespace ParserUtils {

// Platform string parsing
platform::MCE parse_platform(const std::string &str);

// String splitting
std::vector<std::string> split_string(const std::string &str, char delimiter);

enum class DataFormat {
    Unknown,
    NativeJSON,
    NativeCSV,
    MCOfficial
};

// File format detection
DataFormat detect_format(const std::filesystem::path &path);
DataFormat detect_mc_official(const std::filesystem::path &directory);

// File I/O
std::string read_file(const std::filesystem::path &path);
std::vector<std::filesystem::path> find_files(
    const std::filesystem::path &dir,
    const std::string &extension
);

// MC official structure detection
bool is_mc_official_structure(const std::filesystem::path &dir);

// CSV parsing
std::vector<std::string> split_csv_line(const std::string &line);
std::vector<std::vector<std::string>> parse_csv(const std::filesystem::path &path);

// Namespace helpers
std::string qualify_id(const std::string &id, const std::string &default_ns = "minecraft");
std::pair<std::string, std::string> split_namespace(const std::string &qualified_id);

} // namespace ParserUtils
