#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "domain/business/components/TagResolver.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

/// Parser for the MC 1.21+ data-driven format.
///
/// Parses a data-pack directory with the structure:
///   <root>/data/<ns>/enchantment/<id>.json
///   <root>/data/<ns>/tags/item/*.json
///   <root>/data/<ns>/tags/enchantment/*.json
///
/// Also supports parsing from in-memory content via parse_files() —
/// no filesystem access required.
class McOfficialParser {
public:
    using Result = std::pair<
        std::vector<business::loader::EnchantmentData>,
        std::vector<business::loader::EquipmentData>
    >;

    /// Parse a directory following the MC official data-pack layout.
    static Result parse(const std::filesystem::path& directory);

    /// Parse from in-memory file content (no filesystem access).
    ///
    /// @param files  Map of relative data-pack paths → file content strings.
    ///   Enchantment paths: "data/<ns>/enchantment/<id>.json"
    ///   Tag paths:         "data/<ns>/tags/<category>/<name>.json"
    static Result parse_files(
        const std::unordered_map<std::string, std::string>& files
    );

    /// Parse a single enchantment entry from its JSON content.
    /// Useful for fine-grained in-memory usage or unit tests.
    static business::loader::EnchantmentData parse_single_enchantment(
        const std::string& ns,
        const std::string& filename,
        const std::string& content,
        TagResolver& tag_resolver
    );

private:
    /// Derive equipment data from item tag file contents (in-memory).
    static std::vector<business::loader::EquipmentData> derive_equipment_from_tag_files(
        const std::unordered_map<std::string, std::string>& tag_files
    );
};
