#pragma once

#include <optional>
#include <string>
#include <vector>

// ─── Application-level CLI configuration ──────────────────────────────────

struct CLIConfig {
    std::string algorithm = "greedy";
    std::string mode = "direct";
    std::string target;
    std::string wanted;
    std::optional<std::string> input;
    std::optional<std::string> data_pack;
    std::string platform = "auto";
    std::optional<std::string> output;
    std::string format = "text";
    int solutions = 1;
    int memory_mb = 0;
    bool verbose = false;
    bool ignore_cost_cap = false;
    bool help = false;
};

// ─── Application-level spec types ────────────────────────────────────────

struct EnchantmentSpec {
    std::string ns = "minecraft";
    std::string id;
    int level = 1;
};

struct TargetSpec {
    std::string item_id;
    std::vector<EnchantmentSpec> inline_enchants;
};

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
