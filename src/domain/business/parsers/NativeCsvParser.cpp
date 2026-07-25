#include "NativeCsvParser.h"
#include "ParserShared.h"
#include "common/io/CsvIO.h"
#include "common/log/log.hpp"
#include "common/utils/StringUtils.hpp"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

// ── Internal: parse CSV content string into rows ──────────────────────
// Simple CSV parser for the formats used by besq. Supports quoted fields
// with commas inside, and handles the native CSV header+data structure.

csv::CsvTable parse_csv_string(const std::string& content) {
    csv::CsvTable rows;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        std::vector<std::string> fields;
        std::string field;
        bool in_quotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                fields.push_back(std::move(field));
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(std::move(field));
        rows.push_back(std::move(fields));
    }

    return rows;
}

// ── Shared row-processing logic ───────────────────────────────────────

std::vector<business::loader::EnchantmentData>
parse_csv_rows(const csv::CsvTable& rows) {
    using namespace business::loader;
    using namespace business::parser_detail;

    if (rows.empty()) return {};

    TagResolver tag_resolver;

    // First row is header
    const auto& header = rows[0];
    std::unordered_map<std::string, size_t> col_index;
    for (size_t i = 0; i < header.size(); ++i)
        col_index[header[i]] = i;

    auto req_id   = col_index.find("id");
    auto req_max  = col_index.find("max_level");
    auto req_mult = col_index.find("multiplier");
    if (req_id == col_index.end() || req_max == col_index.end() || req_mult == col_index.end()) {
        LOG_WARN("CSV missing required columns (id, max_level, multiplier).");
        return {};
    }

    auto get_field = [&](const std::vector<std::string>& fields,
                         const std::string& col_name) -> const std::string& {
        static const std::string empty;
        auto it = col_index.find(col_name);
        if (it == col_index.end() || it->second >= fields.size()) return empty;
        return fields[it->second];
    };

    std::vector<EnchantmentData> enchantments;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& fields = rows[r];
        if (fields.empty()) continue;

        const std::string& id = get_field(fields, "id");
        if (id.empty()) { LOG_WARN("Skipping CSV row %d with empty id.", r + 1); continue; }

        int32_t max_level = 0;
        try { max_level = std::stoi(get_field(fields, "max_level")); } catch (...) {}
        if (max_level <= 0) { LOG_WARN("Skipping CSV row %d invalid max_level.", r + 1); continue; }

        int32_t multiplier = 0;
        try { multiplier = std::stoi(get_field(fields, "multiplier")); } catch (...) {}
        if (multiplier <= 0) { LOG_WARN("Skipping CSV row %d invalid multiplier.", r + 1); continue; }

        std::string name = get_field(fields, "name");
        if (name.empty()) name = id;

        int32_t limited_level = max_level;
        auto limited_str = get_field(fields, "limited_level");
        if (!limited_str.empty()) { try { limited_level = std::stoi(limited_str); } catch (...) {} }
        if (limited_level <= 0) limited_level = 0;

        std::unordered_set<std::string> exclusive_set;
        std::string excl_str = get_field(fields, "exclusive_set");
        if (!excl_str.empty()) {
            auto items = string_utils::split(excl_str, ';');
            auto resolved = resolve_references(items, tag_resolver);
            exclusive_set = std::move(resolved);
        }

        std::vector<std::string> applicable_items;
        std::string eq_str = get_field(fields, "applicable_equipment");
        if (!eq_str.empty()) {
            auto items = string_utils::split(eq_str, ';');
            auto resolved = resolve_references(items, tag_resolver);
            applicable_items.assign(resolved.begin(), resolved.end());
        }

        EnchantmentData ench;
        ench.id               = id;
        ench.display_name     = std::move(name);
        ench.multiplier       = multiplier;
        ench.max_level        = max_level;
        ench.limited_level    = limited_level;
        ench.exclusive_with.assign(exclusive_set.begin(), exclusive_set.end());
        ench.applicable_to    = std::move(applicable_items);
        enchantments.push_back(std::move(ench));
    }

    return enchantments;
}

} // anonymous namespace

// ============================================================================

std::vector<business::loader::EnchantmentData>
NativeCsvParser::parse_file(const std::filesystem::path& path) {
    return parse_csv_rows(csv::parse(path));
}

std::vector<business::loader::EnchantmentData>
NativeCsvParser::parse(const std::string& content) {
    return parse_csv_rows(parse_csv_string(content));
}
