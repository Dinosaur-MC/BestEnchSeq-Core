#pragma once
#include "types/Equipment.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <vector>

class EquipmentCategoryRegistry;

struct EquipmentParser {
    // Parse native JSON format (equipments array in same file as enchantments)
    static std::vector<Equipment> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Parse native CSV format (equipments.csv)
    static std::vector<Equipment> parse_native_csv(
        const std::filesystem::path &path,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Parse MC official format from data pack directory
    static std::vector<Equipment> parse_mc_official(
        const std::filesystem::path &data_pack_dir,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Auto-detect format and parse
    static std::vector<Equipment> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Serialize to JSON format
    static std::string to_json(
        const std::vector<Equipment> &equipments,
        const EquipmentCategoryRegistry &cat_reg
    );

    // Serialize to CSV format
    static std::string to_csv(
        const std::vector<Equipment> &equipments,
        const EquipmentCategoryRegistry &cat_reg
    );
};
