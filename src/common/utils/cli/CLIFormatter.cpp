// src/common/utils/cli/CLIFormatter.cpp
#include "CLIFormatter.h"

std::string DefaultDiagnosticFormatter::operator()(const Diagnostic& d) const {
    using enum ParseErrorCode;
    switch (d.code) {
        case unknown_option:
            return "error: unknown option '" + std::string(d.arg) + "'";
        case missing_value:
            return "error: option '" + std::string(d.option_name.value_or(d.arg)) + "' requires a value";
        case invalid_value:
            return "error: invalid value '" + std::string(d.arg) + "'";
        case required_missing:
            return "error: required option '--" + std::string(d.option_name.value_or("")) + "' is missing";
        case unexpected_positional:
            return "error: unexpected positional argument '" + std::string(d.arg) + "'";
        default:
            return "error: unknown parse error";
    }
}
