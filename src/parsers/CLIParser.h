#pragma once

#include <string>
#include <vector>

/// A single parsed command-line argument.
struct ParsedArg {
    std::string key;
    std::string value;
    bool is_flag = false;
};

/// Generic key-value CLI argument parser.
///
/// Parses `--key=value`, `--key value`, and `--flag` style arguments, as well as
/// short single-dash flags (`-h` → `help`, `-v` → `verbose`).
///
/// `--` terminates option parsing.  Returns a vector of ParsedArg.
/// Returns an empty vector when called with only the program name (no args),
/// indicating the caller should show usage / help.
///
/// This class has zero business-specific knowledge.  All application-level
/// config interpretation lives in src/cli.h.
class CLIParser {
  public:
    static std::vector<ParsedArg> parse(int argc, char *argv[]);
};
