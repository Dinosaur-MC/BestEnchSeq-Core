#pragma once
#include "types/EquipmentType.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <vector>

struct EquipmentParser {
    // Parse native JSON format (equipments array in same file as enchantments)
    static std::vector<EquipmentType> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Parse native CSV format (equipments.csv)
    static std::vector<EquipmentType> parse_native_csv(
        const std::filesystem::path &path
    );

    // Parse MC official format from data pack directory
    static std::vector<EquipmentType> parse_mc_official(
        const std::filesystem::path &data_pack_dir
    );

    // Auto-detect format and parse
    static std::vector<EquipmentType> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    // Serialize to JSON format
    static std::string to_json(const std::vector<EquipmentType> &equipments);

    // Serialize to CSV format
    static std::string to_csv(const std::vector<EquipmentType> &equipments);
};
