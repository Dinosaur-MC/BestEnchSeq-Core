#pragma once
#include "types/EnchInfo.h"
#include "parser/TagResolver.h"
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
    static std::vector<EnchInfo> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg,
        EnchantmentDataPack *metadata = nullptr
    );

    // Parse native CSV format (enchantments.csv)
    static std::vector<EnchInfo> parse_native_csv(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Parse MC official data-driven format (data/<ns>/enchantment/<id>.json)
    static std::vector<EnchInfo> parse_mc_official(
        const std::filesystem::path &data_pack_dir,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Auto-detect format and parse
    static std::vector<EnchInfo> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Serialize to JSON format
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
