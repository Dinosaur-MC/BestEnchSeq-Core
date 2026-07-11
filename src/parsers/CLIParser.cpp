#include "parsers/CLIParser.h"

#include <iostream>
#include <stdexcept>

// ============================================================================
// Generic key-value CLI argument parser
// ============================================================================

std::vector<ParsedArg> CLIParser::parse(int argc, char *argv[]) {
    std::vector<ParsedArg> result;
    bool options_terminated = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        // -- signals end of options
        if (arg == "--") {
            options_terminated = true;
            break;
        }

        // All options must start with --
        if (arg.size() < 2 || arg[0] != '-' || arg[1] != '-') {
            throw std::runtime_error("Unknown argument: '" + arg + "'");
        }

        std::string key;
        std::string value;

        auto eq_pos = arg.find('=', 2);
        if (eq_pos != std::string::npos) {
            key = arg.substr(2, eq_pos - 2);
            value = arg.substr(eq_pos + 1);
            result.push_back({key, value, false});
        } else {
            key = arg.substr(2);
            // Check if next arg exists and is not another option
            if (i + 1 < argc) {
                std::string next(argv[i + 1]);
                if (next.size() >= 2 && next[0] == '-' && next[1] == '-') {
                    // Next is an option flag → this is a boolean flag
                    if (!key.empty()) {
                        result.push_back({key, "", true});
                    }
                } else {
                    // Next is a value
                    ++i;
                    result.push_back({key, next, false});
                }
            } else {
                // No next arg → boolean flag
                if (!key.empty()) {
                    result.push_back({key, "", true});
                }
            }
        }
    }

    return result;
}
