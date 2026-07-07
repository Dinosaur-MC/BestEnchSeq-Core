#include "parser/ParserUtils.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace ParserUtils {

// ---------------------------------------------------------------------------
// Platform string parsing
// ---------------------------------------------------------------------------
platform::MCE parse_platform(const std::string &str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "java" || lower == "je") {
        return platform::MCE::Java;
    }
    if (lower == "bedrock" || lower == "be") {
        return platform::MCE::Bedrock;
    }
    if (lower == "all" || lower == "both") {
        return platform::MCE::All;
    }
    std::cerr << "Warning: Unknown platform '" << str << "', defaulting to Java." << std::endl;
    return platform::MCE::Java;
}

// ---------------------------------------------------------------------------
// Platform enum to string
// ---------------------------------------------------------------------------
std::string platform_to_string(platform::MCE p) {
    switch (p) {
    case platform::MCE::Java:
        return "java";
    case platform::MCE::Bedrock:
        return "bedrock";
    case platform::MCE::All:
        return "all";
    case platform::MCE::None:
        return "none";
    }
    return "java";
}

// ---------------------------------------------------------------------------
// JSON field extraction helpers
// ---------------------------------------------------------------------------

std::string get_json_string(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return {};
    }
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::String>(val)) {
        return std::get<Json::String>(val);
    }
    return {};
}

int32_t get_json_int(const Json::Object &obj, const std::string &key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return 0;
    }
    auto val = it->second.get_value();
    if (std::holds_alternative<Json::Number>(val)) {
        const auto &num = std::get<Json::Number>(val);
        if (std::holds_alternative<int32_t>(num)) {
            return std::get<int32_t>(num);
        }
        if (std::holds_alternative<int64_t>(num)) {
            int64_t v = std::get<int64_t>(num);
            return static_cast<int32_t>(v);
        }
        if (std::holds_alternative<float>(num)) {
            float v = std::get<float>(num);
            return static_cast<int32_t>(v);
        }
        if (std::holds_alternative<double>(num)) {
            double v = std::get<double>(num);
            return static_cast<int32_t>(v);
        }
    }
    return 0;
}

std::vector<std::string> get_json_string_array(const Json::Object &obj, const std::string &key) {
    std::vector<std::string> result;
    auto it = obj.find(key);
    if (it == obj.end()) {
        return result;
    }
    auto val = it->second.get_value();
    if (!std::holds_alternative<Json::Array>(val)) {
        return result;
    }
    const auto &arr = std::get<Json::Array>(val);
    result.reserve(arr.size());
    for (const auto &elem : arr) {
        auto elem_val = elem.get_value();
        if (std::holds_alternative<Json::String>(elem_val)) {
            result.push_back(std::get<Json::String>(elem_val));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// String splitting
// ---------------------------------------------------------------------------
std::vector<std::string> split_string(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    if (str.empty()) {
        return tokens;
    }
    size_t start = 0;
    while (true) {
        size_t end = str.find(delimiter, start);
        if (end == std::string::npos) {
            if (start < str.size()) {
                tokens.push_back(str.substr(start));
            }
            break;
        }
        if (end > start) {
            tokens.push_back(str.substr(start, end - start));
        }
        start = end + 1;
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// File format detection
// ---------------------------------------------------------------------------

DataFormat detect_format(const std::filesystem::path &path) {
    if (path.empty()) {
        return DataFormat::Unknown;
    }

    if (std::filesystem::is_directory(path)) {
        if (is_mc_official_structure(path)) {
            return DataFormat::MCOfficial;
        }
        return DataFormat::Unknown;
    }

    // Normalise extension to lowercase
    std::string ext = path.extension().string();
    for (auto &c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (ext == ".json") {
        return DataFormat::NativeJSON;
    }
    if (ext == ".csv") {
        return DataFormat::NativeCSV;
    }

    return DataFormat::Unknown;
}

DataFormat detect_mc_official(const std::filesystem::path &directory) {
    if (is_mc_official_structure(directory)) {
        return DataFormat::MCOfficial;
    }
    return DataFormat::Unknown;
}

// ---------------------------------------------------------------------------
// MC official structure detection
// ---------------------------------------------------------------------------

bool is_mc_official_structure(const std::filesystem::path &dir) {
    if (!std::filesystem::is_directory(dir)) {
        return false;
    }

    std::filesystem::path data_dir = dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return false;
    }

    // Look for a namespace directory that contains enchantment/ or tags/
    for (const auto &ns_entry : std::filesystem::directory_iterator(data_dir)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        for (const auto &sub_entry : std::filesystem::directory_iterator(ns_entry.path())) {
            if (!sub_entry.is_directory()) {
                continue;
            }
            std::string dirname = sub_entry.path().filename().string();
            if (dirname == "enchantment" || dirname == "tags") {
                return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

std::string read_file(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File not found: " + path.string());
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::filesystem::path> find_files(
    const std::filesystem::path &dir,
    const std::string &extension
) {
    std::vector<std::filesystem::path> result;

    if (!std::filesystem::is_directory(dir)) {
        return result;
    }

    // Normalise extension: ensure it starts with '.'
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (ext.empty() || entry.path().extension() == ext) {
                result.push_back(entry.path());
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Namespace helpers
// ---------------------------------------------------------------------------

std::pair<std::string, std::string> split_namespace(const std::string &qualified_id) {
    size_t colon_pos = qualified_id.find(':');
    if (colon_pos == std::string::npos) {
        return {std::string(), qualified_id};
    }
    return {
        qualified_id.substr(0, colon_pos),
        qualified_id.substr(colon_pos + 1)
    };
}

std::string qualify_id(const std::string &id, const std::string &default_ns) {
    if (id.find(':') != std::string::npos) {
        return id;
    }
    return default_ns + ":" + id;
}

} // namespace ParserUtils
