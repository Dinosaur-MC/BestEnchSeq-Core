#pragma once
#include "types/Equipment.h"
#include "types/RawEnchInfo.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <vector>

class EquipmentCategoryRegistry;

struct EquipmentParser {
    // Parse native JSON format (equipments array in same file as enchantments)
    // Returns string-based intermediate data (RawEquipment).
    static std::vector<RawEquipment> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Parse native CSV format (equipments.csv)
    static std::vector<RawEquipment> parse_native_csv(
        const std::filesystem::path &path
    );

    // Parse MC official format from data pack directory
    static std::vector<RawEquipment> parse_mc_official(
        const std::filesystem::path &data_pack_dir
    );

    // Auto-detect format and parse
    static std::vector<RawEquipment> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Serialize to JSON format (requires category registry for ID→name resolution)
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
