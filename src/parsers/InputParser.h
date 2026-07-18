#pragma once
#include "BESQTypes.h"
#include "cli/cli.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

/// Domain-level parsed input (produced by InputParser, consumed by
/// CompactAdapter to build compact AlgorithmInput).
struct ParsedInput {
    MCE platform;
    EnchSet source_ench;           // source enchantments (existing on target)
    EnchSet target_ench;           // target enchantments to achieve
    ItemStack target_item;         // target equipment
    ItemCollection available_items;
};

class InputParser {
  public:
    static ItemCollection parse_inventory(
        const std::filesystem::path &path,
        const std::unordered_map<std::string, int32_t> &ench_id_map,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static ItemStack build_target(
        const TargetSpec &target_spec,
        const std::unordered_map<std::string, int32_t> &ench_id_map,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static EnchSet build_wanted_enchset(
        const std::vector<EnchantmentSpec> &wanted,
        const std::unordered_map<std::string, int32_t> &ench_id_map
    );

    static ParsedInput assemble_input(
        const CLIConfig& cli_config,
        const std::unordered_map<std::string, int32_t> &ench_id_map,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static ItemCollection generate_books(
        const EnchSet &wanted,
        const EnchSet &existing
    );
};
