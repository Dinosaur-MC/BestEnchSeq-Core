#include "parsers/CLIParser.h"
#include "log/log.hpp"

#include <cstring>
#include <stdexcept>

namespace {

/// Short → long name mapping.
/// Extend this array when new short flags are added.
const char *lookup_short(const char *short_name) {
    static const struct { const char *short_name; const char *long_name; } map[] = {
        {"h", "help"},
        {"v", "verbose"},
        {"V", "version"},
    };
    for (const auto &entry : map) {
        if (std::strcmp(short_name, entry.short_name) == 0)
            return entry.long_name;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// Generic key-value CLI argument parser
// ============================================================================

std::vector<ParsedArg> CLIParser::parse(int argc, char *argv[]) {
    std::vector<ParsedArg> result;

    // No args beyond program name → caller should show help
    if (argc <= 1) {
        return result;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        // -- signals end of options
        if (arg == "--") {
            // Warn about any remaining arguments after --
            for (int j = i + 1; j < argc; ++j) {
                LOG_WARN("Warning: argument '%s' after -- ignored", argv[j]);
            }
            break;
        }

        // ---- long options (--key) -----------------------------------------
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
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
                    if (!next.empty() && next[0] == '-') {
                        result.push_back({key, "", true});
                    } else {
                        ++i;
                        result.push_back({key, next, false});
                    }
                } else {
                    result.push_back({key, "", true});
                }
            }
            continue;
        }

        // ---- short options (-key) -----------------------------------------
        if (arg.size() >= 2 && arg[0] == '-') {
            std::string short_name = arg.substr(1);
            const char *long_name = lookup_short(short_name.c_str());
            if (long_name) {
                result.push_back({long_name, "", true});
            } else {
                throw std::runtime_error("Unknown option: '" + arg + "'");
            }
            continue;
        }

        // ---- positional arguments (after options terminated) ----
        // For now, any non-option before -- is an error in the generic parser.
        // If we reach here, the argument didn't start with -.
        // This can happen with `-k value` where the value itself starts with -
        // but wasn't consumed because we don't know which flags take values.
        // The business layer (parse_cli) handles this.
        throw std::runtime_error("Unexpected argument: '" + arg + "'");
    }

    return result;
}
