#pragma once
#include "BESQTypes.h"
#include "parser/CLIParser.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

/// Domain-level parsed input (produced by InputParser, consumed by
/// CompactAdapter to build compact AlgorithmInput).
struct ParsedInput {
    platform::MCE platform;
    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
};

class InputParser {
  public:
    static ItemCollection parse_inventory(
        const std::filesystem::path &path,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static ItemStack build_target(
        const TargetSpec &target_spec,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    static EnchSet build_wanted_enchset(
        const std::vector<EnchantmentSpec> &wanted,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    static ParsedInput assemble_input(
        const CLIConfig& cli_config,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    static ItemCollection generate_books(
        const EnchSet &wanted,
        const EnchSet &existing
    );
};
