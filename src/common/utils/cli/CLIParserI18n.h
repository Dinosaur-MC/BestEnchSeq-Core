// src/common/utils/cli/CLIParserI18n.h
#pragma once

#include "CLICommon.h"
#include <string>
#include <string_view>

// ============================================================================
// DiagnosticTranslator Concept
// ============================================================================

template<typename F>
concept DiagnosticTranslator = requires(F& f, const Diagnostic& diag) {
    { f(diag) } -> std::convertible_to<std::string>;
};

// ============================================================================
// DefaultDiagnosticFormatter — English output, no external dependencies
// ============================================================================

struct DefaultDiagnosticFormatter {
    std::string operator()(const Diagnostic& diag) const;
};

// ============================================================================
// HelpTranslator Concept
// ============================================================================

template<typename F>
concept HelpTranslator = requires(F& f, std::string_view key) {
    { f(key) } -> std::convertible_to<std::string>;
};
