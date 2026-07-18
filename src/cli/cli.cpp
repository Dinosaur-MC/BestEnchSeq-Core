#include "cli.h"
#include "parsers/CLIParser.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "utils/ParserUtils.hpp"
#include "log/log.hpp"

#include <cctype>
#include <iostream>
#include <stdexcept>

// ============================================================================
// Help text
// ============================================================================
std::string get_cli_help_text(const std::string &program_name) {
    return
        "Usage: " + program_name + " [options] --target <item> [--source <enchants>]\n"
        "   or: " + program_name + " (no args: show this help)\n"
        "\n"
        "Options:\n"
        "  -h, --help              Show this help message\n"
        "  --algorithm <name>      Search algorithm: greedy (default), dfs, astar,\n"
        "                           penalty_balance, hierarchical, idastar,\n"
        "                           hamming, or difficulty_first\n"
        "  --target <spec>         Target item (e.g., diamond_sword or diamond_sword[sharpness=3])\n"
        "  --source <list>         Source enchantments already on the target (e.g., efficiency=4,unbreaking=3)\n"
        "  --mode <mode>           Operation mode: direct (default) or inventory\n"
        "  --platform <platform>   Platform: java, bedrock, or auto (default)\n"
        "  --format <format>       Output format: text (default), compact, or json\n"
        "  --solutions <n>         Maximum solutions (0 = unlimited, default: 1)\n"
        "  --input <file>          Input file path (inventory mode)\n"
        "  --output <file>         Output file path (default: stdout)\n"
        "  --data-pack <dir>       Custom data pack directory\n"
        "  --registry-dir <dir>    Custom registry directory\n"
        "  --registries <name>     Registry name/version (default: minecraft:latest)\n"
        "  --ignore-cost-cap       Bypass the survival-mode 39-level cap (for modded play)\n"
        "  --memory <MB|auto>      Memory budget for AStar search (default: auto)\n"
        "  -v, --verbose           Show algorithm diagnostic counters on completion\n"
        "\n"
        "Enchantment formats:\n"
        "  id=level                e.g., sharpness=5\n"
        "  ns:id=level             e.g., minecraft:sharpness=5\n"
        "  id:level                e.g., sharpness:5 (colon shorthand)\n";
}

// ============================================================================
// Enchantment spec parsing
// ============================================================================

EnchantmentSpec parse_enchantment(const std::string &spec) {
    auto results = EnchParser::parse(spec);
    if (results.empty())
        throw std::runtime_error("Empty enchantment spec");
    return results[0];
}

// ============================================================================

std::vector<EnchantmentSpec> parse_enchantment_list(const std::string &list) {
    return EnchParser::parse(list);
}

// ============================================================================
// Target spec parsing
// ============================================================================

TargetSpec parse_target(const std::string &target) {
    return ItemParser::parse(target);
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
        if (key == "verbose") {
            config.verbose = true;
            continue;
        }
        if (key == "ignore-cost-cap") {
            config.ignore_cost_cap = true;
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
        } else if (key == "wanted") {
            LOG_WARN("Warning: --wanted is deprecated, use --source instead");
            config.source = value;
        } else if (key == "source") {
            config.source = value;
        } else if (key == "registry-dir") {
            config.registry_dir = value;
        } else if (key == "registries") {
            config.registries = value;
        } else if (key == "input") {
            config.input = value;
        } else if (key == "output") {
            config.output = value;
        } else if (key == "data-pack") {
            config.data_pack = value;
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
                if (n > 100000) throw std::runtime_error("--solutions must be <= 100000\n");
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
    if (!config.help) {
        if (config.target.empty())
            throw std::runtime_error("Missing required argument: --target\n");
    }

    return config;
}
