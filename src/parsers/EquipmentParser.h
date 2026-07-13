#pragma once
#include "types/RawTypes.h"
#include "utils/TagResolver.hpp"
#include <filesystem>
#include <vector>

struct EquipmentParser {
    /// Parse native JSON format (equipments array in same file as enchantments).
    /// Returns string-based intermediate data (RawEquipment).
    static std::vector<RawEquipment> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    /// Parse native CSV format (equipments.csv).
    static std::vector<RawEquipment> parse_native_csv(
        const std::filesystem::path &path
    );

    /// Parse MC official format from data pack directory.
    static std::vector<RawEquipment> parse_mc_official(
        const std::filesystem::path &data_pack_dir
    );

    /// Auto-detect format and parse.
    static std::vector<RawEquipment> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );
};
