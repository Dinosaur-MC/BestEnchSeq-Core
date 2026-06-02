#pragma once
#include "types/EnchInfo.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <string>
#include <vector>

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
        EnchantmentDataPack *metadata = nullptr
    );

    // Auto-detect format and parse (placeholder for CSV/MC official in Task 5)
    static std::vector<EnchInfo> parse(
        const std::filesystem::path &path,
        TagResolver &tag_resolver
    );
};
