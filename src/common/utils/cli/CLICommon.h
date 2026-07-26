#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Parse Error Codes
// ============================================================================

enum class ParseErrorCode : uint8_t {
    ok,
    unknown_option,
    missing_value,
    invalid_value,
    required_missing,
    unexpected_positional,
};

// ============================================================================
// Diagnostic — error accumulation unit
// ============================================================================

struct Diagnostic {
    ParseErrorCode code;
    std::string_view arg;                     // raw argument fragment
    std::optional<std::string_view> option_name; // affected option long name
};

// ============================================================================
// Parsable Concept — types that can be parsed from string
// ============================================================================

// Forward declarations for entry types (defined in CLIParser.h)
template<typename T> struct Option;
struct Flag;
template<typename T> struct Positional;

// Free-function from_string — user-extensible via ADL / specialization
// Returns true on success, false on failure (no exceptions)
bool from_string(std::string_view sv, int& out) noexcept;
bool from_string(std::string_view sv, long& out) noexcept;
bool from_string(std::string_view sv, float& out) noexcept;
bool from_string(std::string_view sv, double& out) noexcept;
bool from_string(std::string_view sv, bool& out) noexcept;
bool from_string(std::string_view sv, std::string& out) noexcept;

template<typename T>
concept Parsable = requires(std::string_view sv, T& out) {
    { from_string(sv, out) } -> std::convertible_to<bool>;
};

// ============================================================================
// OptionValue Type Mapping — maps entry types to their value types
// ============================================================================

template<typename T> struct OptionValueType;
template<Parsable T> struct OptionValueType<Option<T>> { using type = std::optional<T>; };
template<>           struct OptionValueType<Flag>       { using type = bool; };
template<Parsable T> struct OptionValueType<Positional<T>> { using type = std::optional<T>; };

template<typename T>
using OptionValue = typename OptionValueType<std::decay_t<T>>::type;
