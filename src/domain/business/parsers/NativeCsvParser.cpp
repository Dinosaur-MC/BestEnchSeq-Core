#include "NativeCsvParser.h"
#include "ParserShared.h"
#include "common/io/CsvIO.h"
#include "common/log/log.hpp"
#include "common/utils/ParserUtils.hpp"

#include <unordered_map>
#include <vector>

std::vector<business::loader::EnchantmentData>
NativeCsvParser::parse_file(const std::filesystem::path& path) {
    using namespace business::loader;
    using namespace business::parser_detail;

    TagResolver tag_resolver;
    auto rows = csv::parse(path);
    if (rows.empty()) return {};

    // First row is header — map column names to indices
    const auto& header = rows[0];
    std::unordered_map<std::string, size_t> col_index;
    for (size_t i = 0; i < header.size(); ++i) {
        col_index[header[i]] = i;
    }

    auto req_id   = col_index.find("id");
    auto req_max  = col_index.find("max_level");
    auto req_mult = col_index.find("multiplier");
    if (req_id == col_index.end() || req_max == col_index.end() || req_mult == col_index.end()) {
        LOG_WARN("CSV file missing required columns (id, max_level, multiplier).");
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
        if (id.empty()) {
            LOG_WARN("Skipping CSV row %d with empty id.", r + 1);
            continue;
        }

        int32_t max_level = 0;
        try { max_level = std::stoi(get_field(fields, "max_level")); } catch (...) {}
        if (max_level <= 0) {
            LOG_WARN("Skipping CSV row %d with invalid max_level.", r + 1);
            continue;
        }

        int32_t multiplier = 0;
        try { multiplier = std::stoi(get_field(fields, "multiplier")); } catch (...) {}
        if (multiplier <= 0) {
            LOG_WARN("Skipping CSV row %d with invalid multiplier.", r + 1);
            continue;
        }

        std::string name = get_field(fields, "name");
        if (name.empty()) name = id;

        int32_t limited_level = max_level;
        auto limited_str = get_field(fields, "limited_level");
        if (!limited_str.empty()) {
            try { limited_level = std::stoi(limited_str); } catch (...) {}
        }
        if (limited_level <= 0) limited_level = 0;

        // Exclusive set — semicolon-separated tokens
        std::unordered_set<std::string> exclusive_set;
        std::string excl_str = get_field(fields, "exclusive_set");
        if (!excl_str.empty()) {
            auto items    = ParserUtils::split_string(excl_str, ';');
            auto resolved = resolve_references(items, tag_resolver);
            exclusive_set = std::move(resolved);
        }

        // Applicable items — semicolon-separated tokens
        std::vector<std::string> applicable_items;
        std::string eq_str = get_field(fields, "applicable_equipment");
        if (!eq_str.empty()) {
            auto items    = ParserUtils::split_string(eq_str, ';');
            auto resolved = resolve_references(items, tag_resolver);
            applicable_items.assign(resolved.begin(), resolved.end());
        }

        EnchantmentData ench;
        ench.id               = id;
        ench.display_name     = std::move(name);
        ench.multiplier       = multiplier;
        ench.max_level        = max_level;
        ench.limited_level    = limited_level;
        ench.exclusive_with   = std::vector<std::string>(exclusive_set.begin(), exclusive_set.end());
        ench.applicable_to    = std::move(applicable_items);
        enchantments.push_back(std::move(ench));
    }

    return enchantments;
}
