// src/common/utils/cli/CLIFormatter.h
#pragma once

#include "CLICommon.h"
#include <string>
#include <string_view>

// ============================================================================
// DiagnosticTranslator Concept — translates Diagnostic → human-readable string
// ============================================================================

template<typename F>
concept DiagnosticTranslator = requires(F& f, const Diagnostic& diag) {
    { f(diag) } -> std::convertible_to<std::string>;
};

// ============================================================================
// HelpTranslator Concept — translates help key → localized string
// ============================================================================

template<typename F>
concept HelpTranslator = requires(F& f, std::string_view key) {
    { f(key) } -> std::convertible_to<std::string>;
};

// ============================================================================
// DefaultHelpFormatter — returns the key as-is (English fallback)
// ============================================================================

struct DefaultHelpFormatter {
    std::string operator()(std::string_view key) const noexcept {
        return std::string(key);
    }
};

// ============================================================================
// DefaultDiagnosticFormatter — English error messages
// ============================================================================

struct DefaultDiagnosticFormatter {
    std::string operator()(const Diagnostic& d) const;
};

// ============================================================================
// UnifiedDefaultFormatter — handles both help and diagnostics (English)
// ============================================================================

struct UnifiedDefaultFormatter {
    std::string operator()(std::string_view key) const {
        return DefaultHelpFormatter{}(key);
    }
    std::string operator()(const Diagnostic& d) const {
        return DefaultDiagnosticFormatter{}(d);
    }
};
