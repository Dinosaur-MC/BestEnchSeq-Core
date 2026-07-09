#pragma once
#include "algorithm/IAlgorithm.h"
#include "parser/CLIParser.h"
#include "parser/TagResolver.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

class InputParser {
  public:
    // Parse inventory JSON file into ItemCollection
    static ItemCollection parse_inventory(
        const std::filesystem::path &path,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry
    );

    // Build target ItemStack from target spec + inline enchantments
    static ItemStack build_target(
        const TargetSpec &target_spec,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    // Build the wanted EnchSet from wanted enchantment list
    static EnchSet build_wanted_enchset(
        const std::vector<EnchantmentSpec> &wanted,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    // Full assembly: CLI config to algorithm Input
    static AlgorithmInput assemble_input(
        const CLIConfig &cli_config,
        const std::unordered_map<std::string, const Equipment*> &equipment_registry,
        const std::unordered_map<std::string, int32_t> &ench_name_to_id
    );

    // Generate books for missing/upgrade enchants (direct mode)
    static ItemCollection generate_books(
        const EnchSet &wanted,
        const EnchSet &existing
    );
};
