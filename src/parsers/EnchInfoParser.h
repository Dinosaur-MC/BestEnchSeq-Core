#pragma once
#include "types/RawTypes.h"
#include "parsers/TagResolver.h"
#include <filesystem>
#include <string>
#include <vector>

struct EnchantmentDataPack;

struct EnchInfoParser {
    /// Parse native JSON format — single file with enchantments array.
    /// Returns string-based intermediate data (RawEnchInfo).
    static std::vector<RawEnchInfo> parse_native_json(
        const std::filesystem::path &path,
        TagResolver &tag_resolver,
        EnchantmentDataPack *metadata = nullptr
    );

    /// Parse native CSV format (enchantments.csv).
    static std::vector<RawEnchInfo> parse_native_csv(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );

    /// Parse MC official data-driven format (data/<ns>/enchantment/<id>.json).
    static std::vector<RawEnchInfo> parse_mc_official(
        const std::filesystem::path &data_pack_dir,
        TagResolver &tag_resolver
    );

    /// Auto-detect format and parse.
    static std::vector<RawEnchInfo> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );
};
