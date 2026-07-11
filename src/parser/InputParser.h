#pragma once
#include "BESQTypes.h"
#include "parser/CLIParser.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

class EnchantmentRegistry;

/// Domain-level parsed input (produced by InputParser, consumed by
/// CompactAdapter to build compact AlgorithmInput).
struct ParsedInput {
    MCE platform;
    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
};

class InputParser {
  public:
    static ItemCollection parse_inventory(
        const std::filesystem::path &path,
        const EnchantmentRegistry &ench_reg,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static ItemStack build_target(
        const TargetSpec &target_spec,
        const EnchantmentRegistry &ench_reg,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static EnchSet build_wanted_enchset(
        const std::vector<EnchantmentSpec> &wanted,
        const EnchantmentRegistry &ench_reg
    );

    static ParsedInput assemble_input(
        const CLIConfig& cli_config,
        const EnchantmentRegistry &ench_reg,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    static ItemCollection generate_books(
        const EnchSet &wanted,
        const EnchSet &existing
    );
};
