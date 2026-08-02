#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/registries/TagRegistry.h"

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
    /// A single item-tag definition parsed from `data/<ns>/tags/item/<path>.json`.
    /// Carried alongside enchantments+equipment so datapack-defined item tags
    /// survive into the profile's tag universe and TagResolver (B-T14 I-1).
    struct ItemTagDefinition {
        std::string key;                  ///< "<ns>:<tagpath>" (no '#')
        std::vector<std::string> values;  ///< raw entries (concrete IDs or "#refs")
        bool replace = false;             ///< MC "replace" flag
    };

    /// Parser result: enchantments + equipment + the datapack's own item-tag
    /// definitions (`data/<ns>/tags/item/*.json`).
    struct Result {
        std::vector<business::loader::EnchantmentData> enchantments;
        std::vector<business::loader::EquipmentData> equipment;
        std::vector<ItemTagDefinition> item_tags;
    };

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

    /// Build a TagRegistry from the datapack's item-tag definitions, skipping
    /// tags whose ids fail NSID validation (uppercase, `.`/`..` segments, … —
    /// logged with a warning, mirroring `ProfileManager::load_datapack`).
    /// Each registry entry maps the tag NSID (`NSID("#" + key)`) → key.
    static TagRegistry build_item_tag_registry(
        const std::vector<ItemTagDefinition>& item_tags
    );

    /// Load each item-tag definition into \p resolver (honoring its `replace`
    /// flag) on top of whatever universe the resolver already holds (typically
    /// the seeded vanilla tags).  Invalid tag ids are skipped.  A no-op for an
    /// empty \p item_tags.
    static void load_item_tags_into(
        TagResolver& resolver,
        const std::vector<ItemTagDefinition>& item_tags
    );

private:
    /// Derive equipment data from item tag file contents (in-memory).
    static std::vector<business::loader::EquipmentData> derive_equipment_from_tag_files(
        const std::unordered_map<std::string, std::string>& tag_files
    );

    /// Extract item-tag definitions (`data/<ns>/tags/item/*.json`) in-memory.
    static std::vector<ItemTagDefinition> extract_item_tag_definitions(
        const std::unordered_map<std::string, std::string>& tag_files
    );
};
