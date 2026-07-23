#pragma once
#include "domain/interface/types/RawTypes.h"
#include <filesystem>
#include <string>
#include <vector>

/// Metadata about a data pack (optional header in native JSON files).
struct EnchantmentDataPack {
    std::string name;
    std::string description;
    std::string author;
    std::string version;
};

struct EnchInfoParser {
    /// Parse native all-in-one JSON format (current vanilla.json).
    /// Returns a pair of (enchantments, equipment).
    /// Output RawEnchantment have pre-resolved exclusive_set/applicable_items
    /// (no `#` references).
    static std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
    parse_native_json(const std::filesystem::path &path,
                      EnchantmentDataPack *metadata = nullptr);

    /// Parse native JSON from an in-memory string (e.g. embedded data).
    /// Same as parse_native_json(path) but skips file I/O.
    static std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
    parse_native_json_str(const std::string &content,
                          EnchantmentDataPack *metadata = nullptr);

    /// Parse native CSV format (enchantments.csv).
    /// Equipment cannot be represented in CSV format — returns empty equipment
    /// vector.
    static std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
    parse_native_csv(const std::filesystem::path &path);

    /// Parse MC official data-driven format (data/<ns>/enchantment/<id>.json).
    /// Equipment is derived from item tag files.
    static std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
    parse_mc_official(const std::filesystem::path &dir);

    /// Auto-detect format and dispatch.
    static std::pair<std::vector<RawEnchantment>, std::vector<RawEquipment>>
    parse(const std::filesystem::path &path,
          EnchantmentDataPack *metadata = nullptr);
};
