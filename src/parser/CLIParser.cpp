#include "parser/CLIParser.h"
#include "parser/ParserUtils.h"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------
std::string CLIParser::get_help_text(const std::string &program_name) {
    return
        "Usage: " + program_name + " [options] --target <item> --wanted <enchants>\n"
        "\n"
        "Options:\n"
        "  --help                  Show this help message\n"
        "  --algorithm <name>      Search algorithm: greedy (default), dfs, astar,\n"
        "                           penalty_balance, or hierarchical\n"
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
        "\n"
        "Enchantment formats:\n"
        "  id=level                e.g., sharpness=5\n"
        "  ns:id=level             e.g., minecraft:sharpness=5\n"
        "  id:level                e.g., sharpness:5 (colon shorthand)\n";
}

// ---------------------------------------------------------------------------
// Enchantment spec parsing
// ---------------------------------------------------------------------------

// Check whether a string consists entirely of digit characters.
static bool is_all_digits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

EnchantmentSpec CLIParser::parse_enchantment(const std::string &spec) {
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

// ---------------------------------------------------------------------------
// Enchantment list parsing
// ---------------------------------------------------------------------------
std::vector<EnchantmentSpec> CLIParser::parse_enchantment_list(const std::string &list) {
    std::vector<EnchantmentSpec> result;
    auto tokens = ParserUtils::split_string(list, ',');
    for (const auto &token : tokens) {
        result.push_back(parse_enchantment(token));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Target spec parsing
// ---------------------------------------------------------------------------
TargetSpec CLIParser::parse_target(const std::string &target) {
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

// ---------------------------------------------------------------------------
// Main argument parser
// ---------------------------------------------------------------------------
CLIConfig CLIParser::parse(int argc, char *argv[]) {
    CLIConfig config;
    std::string program_name = "besq";
    if (argc > 0) {
        program_name = argv[0];
        auto it = program_name.find_last_of("/\\");
        program_name = program_name.substr(it + 1);
    }
    bool options_terminated = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        // -- signals end of options; everything after is positional
        if (arg == "--") {
            options_terminated = true;
            break;
        }

        // All options must start with --
        if (arg.size() < 2 || arg[0] != '-' || arg[1] != '-') {
            throw std::runtime_error("Unknown argument: '" + arg + "'\n" + get_help_text(program_name));
        }

        std::string key;
        std::string value;
        bool has_equals = false;

        auto eq_pos = arg.find('=', 2); // search from after "--"
        if (eq_pos != std::string::npos) {
            key = arg.substr(2, eq_pos - 2);
            value = arg.substr(eq_pos + 1);
            has_equals = true;
        } else {
            key = arg.substr(2);
        }

        // Boolean flags with no value
        if (key == "help") {
            config.help = true;
            std::cout << get_help_text(program_name) << std::endl;
            continue;
        }
        if (key == "ignore-cost-cap") {
            config.ignore_cost_cap = true;
            continue;
        }

        // All remaining options need a value
        if (!has_equals) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --" + key + "\n" + get_help_text(program_name));
            }
            value = argv[++i];
        }

        if (key == "mode") {
            if (value != "direct" && value != "inventory") {
                throw std::runtime_error(
                    "Invalid mode: '" + value + "'. Expected 'direct' or 'inventory'.\n" + get_help_text(program_name)
                );
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
                throw std::runtime_error(
                    "Invalid platform: '" + value + "'. Expected 'java', 'bedrock', or 'auto'.\n" + get_help_text(program_name)
                );
            }
            config.platform = value;
        } else if (key == "format") {
            if (value != "text" && value != "compact" && value != "json") {
                throw std::runtime_error(
                    "Invalid format: '" + value + "'. Expected 'text', 'compact', or 'json'.\n" + get_help_text(program_name)
                );
            }
            config.format = value;
        } else if (key == "algorithm") {
            config.algorithm = value;
        } else if (key == "solutions") {
            try {
                int n = std::stoi(value);
                if (n < 0) {
                    throw std::runtime_error(
                        "Invalid --solutions value: " + value + ". Must be >= 0.\n" + get_help_text(program_name)
                    );
                }
                config.solutions = n;
            } catch (const std::exception &) {
                throw std::runtime_error(
                    "Invalid --solutions value: '" + value + "'. Expected an integer.\n" + get_help_text(program_name)
                );
            }
        } else {
            throw std::runtime_error("Unknown option: --" + key + "\n" + get_help_text(program_name));
        }
    }

    // Validate required arguments (skip if --help or -- was used)
    if (!config.help && !options_terminated) {
        if (config.target.empty()) {
            throw std::runtime_error("Missing required argument: --target\n" + get_help_text(program_name));
        }
        if (config.wanted.empty()) {
            throw std::runtime_error("Missing required argument: --wanted\n" + get_help_text(program_name));
        }
    }

    return config;
}
