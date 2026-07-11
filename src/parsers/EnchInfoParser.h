#pragma once
#include "types/EnchInfo.h"
#include "types/RawTypes.h"
#include "parsers/TagResolver.h"
#include <filesystem>
#include <string>
#include <vector>

class EquipmentCategoryRegistry;

struct EnchantmentDataPack {
    std::string name;
    std::string description;
    std::string author;
    std::string version;
};

struct EnchInfoParser {
    // Parse native JSON format — single file with enchantments array
    // Returns string-based intermediate data (RawEnchInfo).
    static std::vector<RawEnchInfo> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        EnchantmentDataPack *metadata = nullptr
    );

    // Parse native CSV format (enchantments.csv)
    static std::vector<RawEnchInfo> parse_native_csv(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Parse MC official data-driven format (data/<ns>/enchantment/<id>.json)
    static std::vector<RawEnchInfo> parse_mc_official(
        const std::filesystem::path &data_pack_dir,
        TagResolver &tag_resolver
    );

    // Auto-detect format and parse
    static std::vector<RawEnchInfo> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Serialize to JSON format (requires category registry for ID→name resolution)
    static std::string to_json(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg,
        const EnchantmentDataPack *metadata = nullptr
    );

    // Serialize to CSV format
    static std::string to_csv(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Export to MC official data-driven format
    static void export_to_mc_official(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg,
        const std::filesystem::path &output_dir
    );
};
