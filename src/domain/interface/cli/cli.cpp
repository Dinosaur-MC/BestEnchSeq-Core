#include "cli.h"
#include "domain/interface/parsers/CLIParser.h"
#include "domain/business/business.h"
#include "BuildConfig.h"
#include "common/utils/ParserUtils.hpp"
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
        "   or: " + program_name + " --export-registry <path> [options]\n"
        "   or: " + program_name + " (no args: show this help)\n"
        "\n"
        "Options:\n"
        "  -h, --help              Show this help message\n"
        "  -V, --version           Show version info\n"
        "  --algorithm <name>      Search algorithm: hamming (default), dfs, astar\n"
        "                           (use --list-algorithms to see all available,\n"
        "                           including externally loaded plugins)\n"
        "  --source <list>         Source enchantments (e.g., sharpness=5,knockback=2)\n"
        "  --target <spec>         Target item with wanted enchantments\n"
        "                           (e.g., diamond_sword[sharpness=3])\n"
        "  --mode <mode>           Operation mode: direct (default) or inventory\n"
        "  --platform <platform>   Platform: java, bedrock, or auto (default)\n"
        "  --solutions <n>         Maximum solutions (0 = unlimited, default: 1, max: " BESQ_STR(BESQ_MAX_SOLUTIONS) ")\n"
        "  --format <format>       Output format: text (default), compact, or json\n"
        "  --input <file>          Input file path (inventory mode)\n"
        "  --output <file>         Output file path (default: stdout)\n"
        "  --registry-dir <dir>    Scan directory for registry data files/subdirs\n"
        "                           (auto-detects JSON, CSV, MC Official format)\n"
        "  --registries <list>     Registry names or paths to activate\n"
        "                           (default: all discovered registries;\n"
        "                           e.g., --registries Vanilla,./custom.json)\n"
        "  --registry-edit <ops>   Runtime registry edits before execution\n"
        "                           Format: <target>:<action>,<id>[,<field>=<val>...]\n"
        "                           Targets: ench | eq | cat  Actions: add | mod | rm\n"
        "                           e.g., --registry-edit \"ench:mod,sharpness,max_level=10\"\n"
        "  --export-registry <path>\n"
        "                           Export current registry to file (.json / .csv)\n"
        "  --config <pairs>        Config key=value pairs (comma-separated).\n"
        "                           Keys: ignore-cost-cap, ignore-penalty-cost,\n"
        "                                 ignore-repair-cost (all: true|false)\n"
        "  --memory <MB|auto>      Memory budget for AStar search (default: auto)\n"
        "  --algo-dir <dir>        Directory with external .so/.dll algorithm strategies\n"
        "  --max-time <seconds>    Max search time in seconds (0 = unlimited, default: 0)\n"
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
        } else if (key == "registry-edit") {
            if (value.empty())
                throw std::runtime_error("Empty --registry-edit value.\n");
            // Basic format validation: must contain at least one ':'
            auto ops = ParserUtils::split_string(value, ';');
            for (const auto& op : ops) {
                if (op.find(':') == std::string::npos)
                    throw std::runtime_error("Invalid --registry-edit operation: '" +
                        op + "'. Expected format <target>:<action>,<id>[,<field>=<val>...]\n");
            }
            config.registry_edit = value;
        } else if (key == "list-algorithms") {
            config.list_algorithms = true;
        } else if (key == "algo-dir") {
            if (value.empty())
                throw std::runtime_error("Empty --algo-dir value.\n");
            config.algo_dir = value;
        } else if (key == "export-registry") {
            if (value.empty())
                throw std::runtime_error("Empty --export-registry value.\n");
            config.export_registry = value;
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
        } else if (key == "max-time") {
            try {
                int n = std::stoi(value);
                if (n < 0) throw std::runtime_error("must be >= 0");
                config.max_time = n;
            } catch (const std::runtime_error &) {
                throw;
            } catch (const std::exception &) {
                throw std::runtime_error("Invalid --max-time value: '" + value + "'. Expected a non-negative integer.\n");
            }
        } else {
            throw std::runtime_error("Unknown option: --" + key + "\n");
        }
    }

    // Validate required arguments
    // --target or --export-registry or --list-algorithms is required
    if (!config.help && !config.version) {
        if (config.target.empty() && !config.export_registry.has_value() && !config.list_algorithms)
            throw std::runtime_error(
                "Missing required argument: --target (or --export-registry to export registry only)\n");
    }

    return config;
}

// ============================================================================
// Registry-aware helpers (replace old unordered_map-based lookup functions)
// ============================================================================

Item build_target(
    const TargetSpec& spec,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg)
{
    // Look up equipment (registry has built-in "minecraft:" fallback)
    auto eq_it = eq_reg.find(NSID(spec.item_id));
    if (eq_it == eq_reg.end())
        throw std::runtime_error("Unknown equipment: '" + spec.item_id + "'");
    const Equipment& equip = *eq_it;

    // Build enchantment set from inline specs
    EnchSet ench_set;
    for (const auto& s : spec.inline_enchants) {
        std::string key = s.ns.empty() ? s.id : s.ns + ":" + s.id;
        auto ench_it = ench_reg.find(NSID(key));
        if (ench_it == ench_reg.end())
            ench_it = ench_reg.find(NSID(s.id));  // bare fallback
        if (ench_it == ench_reg.end())
            throw std::runtime_error("Unknown enchantment: '" + key + "'");
        ench_set.emplace(ench_it->id, ench_it->name, s.level);
    }

    return Item(equip.id, ench_set, 0);
}

EnchSet build_enchset(
    const std::vector<EnchantmentSpec>& specs,
    const EnchantmentRegistry& ench_reg)
{
    EnchSet result;
    for (const auto& s : specs) {
        std::string key = s.ns.empty() ? s.id : s.ns + ":" + s.id;
        auto ench_it = ench_reg.find(NSID(key));
        if (ench_it == ench_reg.end())
            ench_it = ench_reg.find(NSID(s.id));  // bare fallback
        if (ench_it == ench_reg.end())
            throw std::runtime_error("Unknown enchantment: '" + key + "'");
        result.emplace(ench_it->id, ench_it->name, s.level);
    }
    return result;
}

// ============================================================================
// apply_config_pairs — parse --config value and apply to ForgeConfig
// ============================================================================

void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg) {
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
