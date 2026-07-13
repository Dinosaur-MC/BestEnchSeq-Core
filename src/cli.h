#pragma once
#include "types/CLITypes.h"

// ─── Business CLI parsing ────────────────────────────────────────────────

/// Parse CLI arguments into a CLIConfig. Internally uses CLIParser::parse()
/// for raw key-value extraction, then applies business validation.
CLIConfig parse_cli(int argc, char *argv[]);

/// Enchantment spec parsing (e.g. "sharpness=5", "minecraft:sharpness=5")
EnchantmentSpec parse_enchantment(const std::string &spec);
std::vector<EnchantmentSpec> parse_enchantment_list(const std::string &list);

/// Target spec parsing (e.g. "diamond_sword[sharpness=5]")
TargetSpec parse_target(const std::string &target);

/// Business help text describing all options and their semantics.
std::string get_cli_help_text(const std::string &program_name = "besq");
