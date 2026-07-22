#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace csv {

using CsvRow = std::vector<std::string>;
using CsvTable = std::vector<CsvRow>;

// Split a single CSV line into fields (handles quoted fields and escaped quotes)
CsvRow split_line(const std::string &line);

// Read a CSV file and return all rows (first row is typically the header)
CsvTable parse(const std::filesystem::path &path);

// Format a single row as a CSV line (properly quotes fields containing commas or quotes)
std::string format_row(const CsvRow &row);

// Format an entire CSV table (header + data rows) as a string
std::string format(const CsvTable &table);

// Write a CSV table to a file
void write(const std::filesystem::path &path, const CsvTable &table);

} // namespace csv
