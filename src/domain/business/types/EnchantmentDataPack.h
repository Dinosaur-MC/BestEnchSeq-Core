#pragma once
#include <string>

/// Metadata about a data pack (optional header in native JSON files).
struct EnchantmentDataPack {
    std::string name;
    std::string description;
    std::string author;
    std::string version;
};
