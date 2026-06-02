#include "parser/CLIParser.h"

#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------
std::string CLIParser::get_help_text() {
    return
        "Usage: besq [options] --target <item> --wanted <enchants>\n"
        "\n"
        "Options:\n"
        "  --help                  Show this help message\n"
        "  --target <spec>         Target item (e.g., diamond_sword or diamond_sword[sharpness=3])\n"
        "  --wanted <list>         Wanted enchantments (e.g., sharpness=5,knockback=2)\n"
        "  --mode <mode>           Operation mode: direct (default) or inventory\n"
        "  --platform <platform>   Platform: java, bedrock, or auto (default)\n"
        "  --format <format>       Output format: text (default), compact, or json\n"
        "  --solutions <n>         Maximum solutions (0 = unlimited, default: 1)\n"
        "  --input <file>          Input file path (inventory mode)\n"
        "  --output <file>         Output file path (default: stdout)\n"
        "  --data-pack <dir>       Custom data pack directory\n"
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
        result.level = std::stoi(level_str);

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
                result.level = std::stoi(after);
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

    if (list.empty()) {
        return result;
    }

    size_t start = 0;
    while (true) {
        size_t end = list.find(',', start);
        std::string token;
        if (end == std::string::npos) {
            token = list.substr(start);
            if (!token.empty()) {
                result.push_back(parse_enchantment(token));
            }
            break;
        }
        token = list.substr(start, end - start);
        if (!token.empty()) {
            result.push_back(parse_enchantment(token));
        }
        start = end + 1;
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
            throw std::runtime_error("Unknown argument: '" + arg + "'\n" + get_help_text());
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

        // --help is a boolean flag with no value
        if (key == "help") {
            config.help = true;
            std::cout << get_help_text() << std::endl;
            continue;
        }

        // All remaining options need a value
        if (!has_equals) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --" + key + "\n" + get_help_text());
            }
            value = argv[++i];
        }

        if (key == "mode") {
            if (value != "direct" && value != "inventory") {
                throw std::runtime_error(
                    "Invalid mode: '" + value + "'. Expected 'direct' or 'inventory'.\n" + get_help_text()
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
                    "Invalid platform: '" + value + "'. Expected 'java', 'bedrock', or 'auto'.\n" + get_help_text()
                );
            }
            config.platform = value;
        } else if (key == "format") {
            if (value != "text" && value != "compact" && value != "json") {
                throw std::runtime_error(
                    "Invalid format: '" + value + "'. Expected 'text', 'compact', or 'json'.\n" + get_help_text()
                );
            }
            config.format = value;
        } else if (key == "solutions") {
            try {
                int n = std::stoi(value);
                if (n < 0) {
                    throw std::runtime_error(
                        "Invalid --solutions value: " + value + ". Must be >= 0.\n" + get_help_text()
                    );
                }
                config.solutions = n;
            } catch (const std::invalid_argument &) {
                throw std::runtime_error(
                    "Invalid --solutions value: '" + value + "'. Expected an integer.\n" + get_help_text()
                );
            }
        } else {
            throw std::runtime_error("Unknown option: --" + key + "\n" + get_help_text());
        }
    }

    // Validate required arguments (skip if --help or -- was used)
    if (!config.help && !options_terminated) {
        if (config.target.empty()) {
            throw std::runtime_error("Missing required argument: --target\n" + get_help_text());
        }
        if (config.wanted.empty()) {
            throw std::runtime_error("Missing required argument: --wanted\n" + get_help_text());
        }
    }

    return config;
}
