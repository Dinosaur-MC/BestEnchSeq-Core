#pragma once
#include "config/ForgeConfig.h"
#include "types/ItemStack.h"
#include <optional>
#include <string>
#include <vector>

// ─── CLI data types ──────────────────────────────────────────────────────

struct CLIConfig {
    std::string algorithm = "greedy";
    std::string mode = "direct";
    std::string target;
    std::string source;
    std::string config_pairs;                      // raw --config value
    std::optional<std::string> input;
    std::optional<std::string> data_pack;
    std::optional<std::string> registry_dir;
    std::string registries = "minecraft:latest";
    std::string platform = "auto";
    std::optional<std::string> output;
    std::string format = "text";
    int solutions = 1;
    int memory_mb = 0;
    bool verbose = false;
    bool help = false;
    bool version = false;                          // --version / -V
};

struct EnchantmentSpec {
    std::string ns = "minecraft";
    std::string id;
    int level = 1;
};

struct TargetSpec {
    std::string item_id;
    std::vector<EnchantmentSpec> inline_enchants;
};

// ─── Forward declarations (registries included only in .cpp) ─────
class EnchantmentRegistry;
class EquipmentRegistry;

// ─── Business CLI parsing ────────────────────────────────────────────────

/// Parse CLI arguments into a CLIConfig. Internally uses CLIParser::parse()
/// for raw key-value extraction, then applies business validation.
CLIConfig parse_cli(int argc, char *argv[]);

/// Business help text describing all options and their semantics.
std::string get_cli_help_text(const std::string &program_name = "besq");

/// Build an ItemStack from a TargetSpec by resolving equipment name
/// and inline enchantments against the given registries.
/// Throws std::runtime_error if equipment is unknown.
ItemStack build_target(
    const TargetSpec& spec,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg
);

/// Build an EnchSet from parsed EnchantmentSpec[] by resolving
/// enchantment names via the registry (with "minecraft:" fallback).
EnchSet build_enchset(
    const std::vector<EnchantmentSpec>& specs,
    const EnchantmentRegistry& ench_reg
);

/// Parse a --config value and apply recognized key=value pairs to a ForgeConfig.
///
/// Recognized keys: ignore-cost-cap, ignore-penalty-cost, ignore-repair-cost.
/// Each value must be "true" or "false".
/// Throws std::runtime_error on unrecognized keys, malformed syntax, or invalid values.
void apply_config_pairs(const std::string& config_pairs, ForgeConfig& cfg);
