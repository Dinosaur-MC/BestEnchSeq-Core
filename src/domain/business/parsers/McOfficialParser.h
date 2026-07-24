#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <filesystem>
#include <string>
#include <vector>

/// Parser for the MC 1.21+ data-driven format.
///
/// Parses a data-pack directory with the structure:
///   <root>/data/<ns>/enchantment/<id>.json
///   <root>/data/<ns>/tags/item/*.json
///   <root>/data/<ns>/tags/enchantment/*.json
class McOfficialParser {
public:
    using Result = std::pair<
        std::vector<business::loader::EnchantmentData>,
        std::vector<business::loader::EquipmentData>
    >;

    /// Parse a directory following the MC official data-pack layout.
    static Result parse(const std::filesystem::path& directory);
};
