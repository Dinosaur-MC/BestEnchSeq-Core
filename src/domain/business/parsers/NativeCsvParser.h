#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <filesystem>
#include <string>
#include <vector>

/// Parser for the native CSV format.
///
/// CSV format supports enchantment data plus an optional equipment companion
/// file (equipments_<stem>.csv). Enchantment columns: id, name, platform,
/// max_level, limited_level, min_cost_base, min_cost_per_level, multiplier,
/// is_treasure, exclusive_set (semicolon-separated), supported_items.
/// Equipment columns: id, name, category, max_durability.
class NativeCsvParser {
public:
    /// Parse from a CSV file.
    static std::vector<business::loader::EnchantmentData>
    parse_file(const std::filesystem::path& path);

    /// Parse from a CSV string (in-memory content).
    static std::vector<business::loader::EnchantmentData>
    parse(const std::string& content);

    /// Parse an equipment companion file (equipments_<stem>.csv).
    static std::vector<business::loader::EquipmentData>
    parse_equipment_file(const std::filesystem::path& path);

    /// Parse an equipment companion CSV string.
    static std::vector<business::loader::EquipmentData>
    parse_equipment(const std::string& content);
};
