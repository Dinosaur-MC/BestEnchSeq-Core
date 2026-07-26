// src/common/utils/cli/CLIFormatter.h
#pragma once

#include "CLICommon.h"
#include <string>
#include <string_view>

namespace cli {

// ============================================================================
// DiagnosticTranslator concept
// ============================================================================

template<typename F>
concept DiagnosticTranslator = requires(F& f, const Diagnostic& diag) {
    { f(diag) } -> std::convertible_to<std::string>;
};

// ============================================================================
// HelpTranslator concept
// ============================================================================

template<typename F>
concept HelpTranslator = requires(F& f, std::string_view key) {
    { f(key) } -> std::convertible_to<std::string>;
};

// ============================================================================
// DefaultHelpFormatter
// ============================================================================

struct DefaultHelpFormatter {
    std::string operator()(std::string_view key) const noexcept {
        return std::string(key);
    }
};

// ============================================================================
// DefaultDiagnosticFormatter
// ============================================================================

struct DefaultDiagnosticFormatter {
    std::string operator()(const Diagnostic& d) const;
};

// ============================================================================
// UnifiedDefaultFormatter — handles both help text and diagnostics
// ============================================================================

struct UnifiedDefaultFormatter {
    std::string operator()(std::string_view key) const {
        return DefaultHelpFormatter{}(key);
    }
    std::string operator()(const Diagnostic& d) const {
        return DefaultDiagnosticFormatter{}(d);
    }
};

} // namespace cli
