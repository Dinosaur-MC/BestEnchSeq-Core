#include "io/CsvIO.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace csv {

// ---------------------------------------------------------------------------
// Split a single CSV line into fields
// ---------------------------------------------------------------------------

CsvRow split_line(const std::string &line) {
    CsvRow fields;
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

// ---------------------------------------------------------------------------
// Read a CSV file
// ---------------------------------------------------------------------------

CsvTable parse(const std::filesystem::path &path) {
    CsvTable rows;

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open CSV file: " + path.string());
    }

    std::string line;
    while (std::getline(file, line)) {
        // Strip Windows-style line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        rows.push_back(split_line(line));
    }

    return rows;
}

// ---------------------------------------------------------------------------
// Format a single row as a CSV line
// ---------------------------------------------------------------------------

std::string format_row(const CsvRow &row) {
    std::string result;

    for (size_t i = 0; i < row.size(); ++i) {
        if (i > 0) {
            result += ',';
        }

        const auto &field = row[i];
        bool needs_quoting = field.empty() || field.find(',') != std::string::npos ||
                             field.find('"') != std::string::npos ||
                             field.find('\n') != std::string::npos;

        if (needs_quoting) {
            result += '"';
            for (char c : field) {
                if (c == '"') {
                    result += "\"\""; // escape double-quote
                } else {
                    result += c;
                }
            }
            result += '"';
        } else {
            result += field;
        }
    }

    result += '\n';
    return result;
}

// ---------------------------------------------------------------------------
// Format an entire CSV table as a string
// ---------------------------------------------------------------------------

std::string format(const CsvTable &table) {
    std::string result;
    for (const auto &row : table) {
        result += format_row(row);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Write a CSV table to a file
// ---------------------------------------------------------------------------

void write(const std::filesystem::path &path, const CsvTable &table) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not write CSV file: " + path.string());
    }
    file << format(table);
}

} // namespace csv
