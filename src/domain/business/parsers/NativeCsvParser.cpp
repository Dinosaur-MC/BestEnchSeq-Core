#include "NativeCsvParser.h"
#include "ParserShared.h"
#include "common/io/CsvIO.h"
#include "common/io/FileUtils.hpp"
#include "common/log/log.hpp"
#include "domain/business/schemas/EnchInfoSchema.h"
#include "domain/business/schemas/EquipmentSchema.h"

#include <algorithm>
#include <vector>

namespace {

// ── Shared row-processing logic（CSV → DTO，schema 驱动）────────────────

std::vector<business::loader::EnchantmentData>
parse_csv_rows(const csv::CsvTable& rows) {
    using namespace business::loader;
    using namespace business::parser_detail;
    using EnchCsv = business::schema::EnchantmentDataCsv;

    if (rows.empty()) return {};

    TagResolver tag_resolver;
    const auto& header = rows[0];

    // 必填列检查（与旧行为一致：缺 id/max_level/multiplier → 报错返回空）
    auto has_col = [&](const std::string& name) {
        return std::find(header.begin(), header.end(), name) != header.end();
    };
    if (!has_col("id") || !has_col("max_level") || !has_col("multiplier")) {
        LOG_WARN("CSV missing required columns (id, max_level, multiplier).");
        return {};
    }

    std::vector<EnchantmentData> enchantments;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& fields = rows[r];
        if (fields.empty()) continue;

        EnchantmentData ench;
        ds::ErrorList err;
        if (!EnchCsv::parse_row(header, fields, ench, err)) {
            LOG_WARN("Skipping CSV row %zu: %s", r + 1, err.str().c_str());
            continue;
        }
        if (ench.id.empty() || ench.max_level <= 0 || ench.multiplier <= 0) {
            LOG_WARN("Skipping CSV row %zu invalid id/max_level/multiplier.", r + 1);
            continue;
        }
        if (ench.display_name.empty()) ench.display_name = ench.id;

        // exclusive_set: tag 展开；supported_items 原样透传
        auto resolved = resolve_references(ench.exclusive_with, tag_resolver);
        ench.exclusive_with.assign(resolved.begin(), resolved.end());

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
    return parse_csv_rows(csv::parse_string(content));
}

std::vector<business::loader::EquipmentData>
NativeCsvParser::parse_equipment_file(const std::filesystem::path& path) {
    return parse_equipment(file_utils::read_file(path));
}

std::vector<business::loader::EquipmentData>
NativeCsvParser::parse_equipment(const std::string& content) {
    using EqCsv = business::schema::EquipmentDataCsv;
    auto rows = csv::parse_string(content);
    std::vector<business::loader::EquipmentData> result;
    if (rows.empty()) return result;
    const auto& header = rows[0];

    // 必填列检查（与魔咒路径对称：缺 id/category → 报错返回空）
    auto has_col = [&](const std::string& name) {
        return std::find(header.begin(), header.end(), name) != header.end();
    };
    if (!has_col("id") || !has_col("category")) {
        LOG_WARN("Equipment CSV missing required columns (id, category).");
        return result;
    }

    for (size_t r = 1; r < rows.size(); ++r) {
        business::loader::EquipmentData eq;
        ds::ErrorList err;
        if (!EqCsv::parse_row(header, rows[r], eq, err)) {
            LOG_WARN("Skipping equipment CSV row %zu: %s", r + 1, err.str().c_str());
            continue;
        }
        if (eq.id.empty() || eq.category.empty()) continue;
        if (eq.display_name.empty()) eq.display_name = eq.id;
        result.push_back(std::move(eq));
    }
    return result;
}
