#include "cli.h"
#include "parsers/CLIParser.h"
#include "utils/ParserUtils.hpp"

#include <cctype>
#include <iostream>
#include <stdexcept>

// ============================================================================
// Help text
// ============================================================================
std::string get_cli_help_text(const std::string &program_name) {
    return
        "Usage: " + program_name + " [options] --target <item> --wanted <enchants>\n"
        "   or: " + program_name + " (no args: show this help)\n"
        "\n"
        "Options:\n"
        "  -h, --help              Show this help message\n"
        "  --algorithm <name>      Search algorithm: greedy (default), dfs, astar,\n"
        "                           penalty_balance, hierarchical, idastar, or hamming\n"
        "  --target <spec>         Target item (e.g., diamond_sword or diamond_sword[sharpness=3])\n"
        "  --wanted <list>         Wanted enchantments (e.g., sharpness=5,knockback=2)\n"
        "  --mode <mode>           Operation mode: direct (default) or inventory\n"
        "  --platform <platform>   Platform: java, bedrock, or auto (default)\n"
        "  --format <format>       Output format: text (default), compact, or json\n"
        "  --solutions <n>         Maximum solutions (0 = unlimited, default: 1)\n"
        "  --input <file>          Input file path (inventory mode)\n"
        "  --output <file>         Output file path (default: stdout)\n"
        "  --data-pack <dir>       Custom data pack directory\n"
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

static bool is_all_digits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

EnchantmentSpec parse_enchantment(const std::string &spec) {
    EnchantmentSpec result;

    // Look for '=' separator (level)
    auto eq_pos = spec.find('=');
    if (eq_pos != std::string::npos) {
        // Level is everything after '='
        std::string level_str = spec.substr(eq_pos + 1);
        try {
            result.level = std::stoi(level_str);
        } catch (const std::exception &) {
            throw std::runtime_error("Invalid enchantment level: '" + level_str + "' in '" + spec + "'");
        }

        // Spec part is everything before '='
        std::string spec_part = spec.substr(0, eq_pos);

        // Check for namespace separator in spec part
        auto colon_pos = spec_part.find(':');
        if (colon_pos != std::string::npos) {
            result.ns = spec_part.substr(0, colon_pos);
            result.id = spec_part.substr(colon_pos + 1);
        } else {
            result.ns = "minecraft";
            result.id = spec_part;
        }
    } else {
        // No '=' separator — check for colons
        auto colon_pos = spec.find(':');
        if (colon_pos != std::string::npos) {
            std::string after = spec.substr(colon_pos + 1);
            if (is_all_digits(after)) {
                // Colon shorthand: id:level
                result.ns = "minecraft";
                result.id = spec.substr(0, colon_pos);
                try {
                    result.level = std::stoi(after);
                } catch (const std::exception &) {
                    throw std::runtime_error("Invalid enchantment level: '" + after + "' in '" + spec + "'");
                }
            } else {
                // Namespace prefix: ns:id
                result.ns = spec.substr(0, colon_pos);
                result.id = after;
                result.level = 1;
            }
        } else {
            // Plain id, no namespace, no level
            result.ns = "minecraft";
            result.id = spec;
            result.level = 1;
        }
    }

    return result;
}

// ============================================================================

std::vector<EnchantmentSpec> parse_enchantment_list(const std::string &list) {
    std::vector<EnchantmentSpec> result;
    auto tokens = ParserUtils::split_string(list, ',');
    for (const auto &token : tokens) {
        result.push_back(parse_enchantment(token));
    }
    return result;
}

// ============================================================================
// Target spec parsing
// ============================================================================

TargetSpec parse_target(const std::string &target) {
    TargetSpec result;

    auto bracket_pos = target.find('[');
    if (bracket_pos != std::string::npos) {
        auto close_pos = target.find(']', bracket_pos);
        if (close_pos == std::string::npos) {
            throw std::runtime_error("Malformed target spec: missing closing bracket in '" + target + "'");
        }

        result.item_id = target.substr(0, bracket_pos);
        std::string inline_str = target.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
        result.inline_enchants = parse_enchantment_list(inline_str);
    } else {
        result.item_id = target;
    }

    return result;
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
            config.wanted = value;
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
        if (config.wanted.empty())
            throw std::runtime_error("Missing required argument: --wanted\n");
    }

    return config;
}
