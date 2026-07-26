// src/common/utils/cli/CLIParserI18n.cpp
#include "CLIParserI18n.h"

std::string DefaultDiagnosticFormatter::operator()(const Diagnostic& diag) const {
    using enum ParseErrorCode;
    switch (diag.code) {
        case unknown_option:
            return "error: unknown option '" + std::string(diag.arg) + "'";
        case missing_value:
            return "error: option '" + std::string(diag.option_name.value_or(diag.arg)) + "' requires a value";
        case invalid_value:
            return "error: invalid value '" + std::string(diag.arg) + "'";
        case required_missing:
            return "error: required option '--" + std::string(diag.option_name.value_or("")) + "' is missing";
        case unexpected_positional:
            return "error: unexpected positional argument '" + std::string(diag.arg) + "'";
        default:
            return "error: unknown parse error";
    }
}
