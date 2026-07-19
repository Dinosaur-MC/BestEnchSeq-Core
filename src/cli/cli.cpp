#include "cli.h"
#include "parsers/CLIParser.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "types/Equipment.h"
#include "BuildConfig.h"
#include "utils/ParserUtils.hpp"
#include "log/log.hpp"
#include <iostream>
#include <stdexcept>

// Stringify helper so BESQ_MAX_SOLUTIONS appears literally in the help text
#define BESQ_STR2(x) #x
#define BESQ_STR(x)  BESQ_STR2(x)

// ============================================================================
// Help text
// ============================================================================
std::string get_cli_help_text(const std::string &program_name) {
    return
        "Usage: " + program_name + " [options] --target <item>\n"
        "   or: " + program_name + " (no args: show this help)\n"
        "\n"
        "Options:\n"
        "  -h, --help              Show this help message\n"
        "  -V, --version           Show version info\n"
        "  --algorithm <name>      Search algorithm: greedy (default), dfs, astar,\n"
        "                           penalty_balance, hierarchical, or idastar\n"
        "  --source <list>         Source enchantments (e.g., sharpness=5,knockback=2)\n"
        "  --target <spec>         Target item with wanted enchantments\n"
        "                           (e.g., diamond_sword[sharpness=3])\n"
        "  --mode <mode>           Operation mode: direct (default) or inventory\n"
        "  --platform <platform>   Platform: java, bedrock, or auto (default)\n"
        "  --solutions <n>         Maximum solutions (0 = unlimited, default: 1, max: " BESQ_STR(BESQ_MAX_SOLUTIONS) ")\n"
        "  --format <format>       Output format: text (default), compact, or json\n"
        "  --input <file>          Input file path (inventory mode)\n"
        "  --output <file>         Output file path (default: stdout)\n"
        "  --registry-dir <dir>   Scan directory for registry data files/subdirs\n"
        "                           (auto-detects JSON, CSV, MC Official format)\n"
        "  --registries <list>     Registry names or paths to activate\n"
        "                           (default: all discovered registries;\n"
        "                           e.g., --registries Vanilla,./custom.json)\n"
        "  --config <pairs>        Config key=value pairs (comma-separated).\n"
        "                           Keys: ignore-cost-cap, ignore-penalty-cost,\n"
        "                                 ignore-repair-cost (all: true|false)\n"
        "  --memory <MB|auto>      Memory budget for AStar search (default: auto)\n"
        "  -v, --verbose           Show algorithm diagnostic counters on completion\n"
        "\n"
        "Enchantment formats:\n"
        "  id=level                e.g., sharpness=5\n"
        "  ns:id=level             e.g., minecraft:sharpness=5\n"
        "  id:level                e.g., sharpness:5 (colon shorthand)\n";
}

// ============================================================================
// Main CLI argument parsing → CLIConfig
// ============================================================================

CLIConfig parse_cli(int argc, char *argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";
    auto args = CLIParser::parse(argc, argv);

    CLIConfig config;
    if (args.empty()) {
        config.help = true;
        std::cout << get_cli_help_text(prog) << std::endl;
        return config;
    }

    for (const auto &arg : args) {
        const auto &key = arg.key;

        // Boolean flags
        if (key == "help") {
            config.help = true;
            std::cout << get_cli_help_text(prog) << std::endl;
            continue;
        }
        if (key == "version" || key == "V") {
            config.version = true;
            std::cout << prog << " version " << BESQ_VERSION << std::endl;
            continue;
        }
        if (key == "verbose") {
            config.verbose = true;
            continue;
        }
        const auto &value = arg.value;

        if (key == "mode") {
            if (value != "direct" && value != "inventory") {
                throw std::runtime_error("Invalid mode: '" + value + "'. Expected 'direct' or 'inventory'.\n");
            }
            config.mode = value;
        } else if (key == "target") {
            config.target = value;
        } else if (key == "source") {
            config.source = value;
        } else if (key == "registry-dir") {
            config.registry_dir = value;
        } else if (key == "registries") {
            config.registries = value;
        } else if (key == "config") {
            config.config_pairs = value;
            // Validate syntax and recognize keys; actual application
            // to ForgeConfig happens later via apply_config_pairs().
            if (value.empty())
                throw std::runtime_error("Empty --config value.\n");
            auto pairs = ParserUtils::split_string(value, ',');
            for (const auto& pair : pairs) {
                auto eq = pair.find('=');
                if (eq == std::string::npos)
                    throw std::runtime_error("Invalid config pair: '" + pair + "'. Expected key=value format.\n");
                if (eq == 0)
                    throw std::runtime_error("Invalid config pair: '" + pair + "'. Empty key.\n");
                auto k = pair.substr(0, eq);
                auto v = pair.substr(eq + 1);
                if (v.empty())
                    throw std::runtime_error("Invalid config pair: '" + pair + "'. Empty value.\n");
                if (k != "ignore-cost-cap" && k != "ignore-penalty-cost" && k != "ignore-repair-cost")
                    throw std::runtime_error("Unknown config key: '" + k + "'. "
                        "Valid keys: ignore-cost-cap, ignore-penalty-cost, ignore-repair-cost.\n");
                if (v != "true" && v != "false")
                    throw std::runtime_error("Invalid config value for '" + k + "': '" + v + "'. Expected 'true' or 'false'.\n");
            }
        } else if (key == "input") {
            config.input = value;
        } else if (key == "output") {
            config.output = value;
        } else if (key == "platform") {
            if (value != "java" && value != "bedrock" && value != "auto") {
                throw std::runtime_error("Invalid platform: '" + value + "'. Expected 'java', 'bedrock', or 'auto'.\n");
            }
            config.platform = value;
        } else if (key == "format") {
            if (value != "text" && value != "compact" && value != "json") {
                throw std::runtime_error("Invalid format: '" + value + "'. Expected 'text', 'compact', or 'json'.\n");
            }
            config.format = value;
        } else if (key == "algorithm") {
            config.algorithm = value;
        } else if (key == "solutions") {
            try {
                int n = std::stoi(value);
                if (n < 0) throw std::runtime_error("must be >= 0");
                if (n > static_cast<int>(BESQ_MAX_SOLUTIONS))
                    throw std::runtime_error("--solutions must be <= " BESQ_STR(BESQ_MAX_SOLUTIONS) "\n");
                config.solutions = n;
            } catch (const std::runtime_error &) {
                throw;
            } catch (const std::exception &) {
                throw std::runtime_error("Invalid --solutions value: '" + value + "'. Expected an integer.\n");
            }
        } else if (key == "memory") {
            if (value == "auto") {
                config.memory_mb = 0;
            } else {
                try {
                    int n = std::stoi(value);
                    if (n <= 0) throw std::runtime_error("must be positive");
                    if (n > 1048576) throw std::runtime_error("--memory must be <= 1048576\n");
                    config.memory_mb = n;
                } catch (const std::runtime_error &) {
                    throw;
                } catch (const std::exception &) {
                    throw std::runtime_error("Invalid --memory value: '" + value + "'. Expected a positive integer or 'auto'.\n");
                }
            }
        } else {
            throw std::runtime_error("Unknown option: --" + key + "\n");
        }
    }

    // Validate required arguments
    if (!config.help && !config.version) {
        if (config.target.empty())
            throw std::runtime_error("Missing required argument: --target\n");
    }

    return config;
}

// ============================================================================
// Registry-aware helpers (replace old unordered_map-based lookup functions)
// ============================================================================

ItemStack build_target(
    const TargetSpec& spec,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg)
{
    // Look up equipment (registry has built-in "minecraft:" fallback)
    int32_t eq_id = eq_reg.get_id(spec.item_id);
    if (eq_id < 0)
        throw std::runtime_error("Unknown equipment: '" + spec.item_id + "'");
    const Equipment& equip = eq_reg.get(eq_id);

    // Build enchantment set from inline specs
    EnchSet ench_set;
    for (const auto& s : spec.inline_enchants) {
        std::string key = s.ns.empty() ? s.id : s.ns + ":" + s.id;
        int32_t id = ench_reg.get_id(key);
        if (id < 0) {
            id = ench_reg.get_id(s.id);  // bare fallback
        }
        if (id < 0)
            throw std::runtime_error("Unknown enchantment: '" + key + "'");
        ench_set.emplace(id, s.level);
    }

    return ItemStack(equip, ench_set, 0);
}

EnchSet build_enchset(
    const std::vector<EnchantmentSpec>& specs,
    const EnchantmentRegistry& ench_reg)
{
    EnchSet result;
    for (const auto& s : specs) {
        std::string key = s.ns.empty() ? s.id : s.ns + ":" + s.id;
        int32_t id = ench_reg.get_id(key);
        if (id < 0) {
            id = ench_reg.get_id(s.id);  // bare fallback
        }
        if (id < 0)
            throw std::runtime_error("Unknown enchantment: '" + key + "'");
        result.emplace(id, s.level);
    }
    return result;
}

// ============================================================================
// apply_config_pairs — parse --config value and apply to ForgeConfig
// ============================================================================

void apply_config_pairs(const std::string& config_pairs, ForgeConfig& cfg) {
    if (config_pairs.empty()) return;
    auto pairs = ParserUtils::split_string(config_pairs, ',');
    for (const auto& pair : pairs) {
        auto eq = pair.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid config pair: '" + pair + "'. Expected key=value format.\n");
        std::string k = pair.substr(0, eq);
        std::string v = pair.substr(eq + 1);
        bool val = (v == "true");
        if (k == "ignore-cost-cap")
            cfg.ignore_cost_cap = val;
        else if (k == "ignore-penalty-cost")
            cfg.ignore_penalty_cost = val;
        else if (k == "ignore-repair-cost")
            cfg.ignore_repair_cost = val;
        // Unknown keys are already rejected in parse_cli()
    }
}
