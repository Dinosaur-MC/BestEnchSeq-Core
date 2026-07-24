#pragma once
#include "domain/business/types/dto/EnchantmentData.h"

#include <filesystem>
#include <vector>

/// Parser for the native CSV format.
///
/// CSV format supports enchantment data only (no equipment).
/// Columns: id, name, max_level, limited_level, multiplier,
///          exclusive_set (semicolon-separated), applicable_equipment.
class NativeCsvParser {
public:
    /// Parse from a CSV file.
    static std::vector<business::loader::EnchantmentData>
    parse_file(const std::filesystem::path& path);

    /// Parse from a CSV string (in-memory content).
    static std::vector<business::loader::EnchantmentData>
    parse(const std::string& content);
};
