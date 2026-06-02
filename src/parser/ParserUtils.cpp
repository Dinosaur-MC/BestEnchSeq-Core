#include "parser/ParserUtils.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ParserUtils {

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
// CSV parsing
// ---------------------------------------------------------------------------

std::vector<std::string> split_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    if (line.empty()) {
        return fields;
    }

    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (in_quotes) {
            if (c == '"') {
                // Check for escaped double-quote ""
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i; // skip the second quote
                } else {
                    in_quotes = false; // end of quoted field
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
    }

    // Last field (including trailing empty)
    fields.push_back(field);

    return fields;
}

std::vector<std::vector<std::string>> parse_csv(const std::filesystem::path &path) {
    std::vector<std::vector<std::string>> rows;

    std::string content = read_file(path);
    std::stringstream ss(content);
    std::string line;

    while (std::getline(ss, line)) {
        // Strip Windows-style line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        auto fields = split_csv_line(line);
        rows.push_back(std::move(fields));
    }

    return rows;
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
