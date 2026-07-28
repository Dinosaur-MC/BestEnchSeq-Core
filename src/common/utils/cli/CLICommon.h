// src/common/utils/cli/CLICommon.h
#pragma once

#include <array>
#include <cstdint>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

// ============================================================================
// from_string — user-extensible value parsing
// ============================================================================
// Global scope for ADL compatibility. Returns true on success, false on failure.

bool from_string(std::string_view sv, int& out) noexcept;
bool from_string(std::string_view sv, unsigned int& out) noexcept;
bool from_string(std::string_view sv, long& out) noexcept;
bool from_string(std::string_view sv, unsigned long& out) noexcept;
bool from_string(std::string_view sv, float& out) noexcept;
bool from_string(std::string_view sv, double& out) noexcept;
bool from_string(std::string_view sv, bool& out) noexcept;
bool from_string(std::string_view sv, std::string& out);

// ============================================================================
// cli namespace — all CLI parser types
// ============================================================================

namespace cli {

// ============================================================================
// ParseErrorCode
// ============================================================================

enum class ParseErrorCode : uint8_t {
    ok,
    unknown_option,
    missing_value,
    invalid_value,
    required_missing,
    unexpected_positional,
    duplicate_option,
};

// ============================================================================
// Diagnostic
// ============================================================================

struct Diagnostic {
    ParseErrorCode code;
    std::string_view arg;
    std::optional<std::string_view> option_name;
};

// ============================================================================
// Parsable concept
// ============================================================================

template<typename T>
concept Parsable = requires(std::string_view sv, T& out) {
    { from_string(sv, out) } -> std::convertible_to<bool>;
};

// ============================================================================
// Entry types
// ============================================================================

template<Parsable T>
struct Option {
    using value_type = T;
    std::string_view  long_name;
    char              short_name = '\0';
    std::string_view  help_key;
    std::string_view  help_group;
    std::optional<T>  default_v;
    bool              required = false;
};

struct Flag {
    using value_type = bool;
    std::string_view  long_name;
    char              short_name = '\0';
    std::string_view  help_key;
    std::string_view  help_group;
};

template<Parsable T>
struct Positional {
    using value_type = T;
    std::string_view  name;
    std::string_view  help_key;
    std::optional<T>  default_v;
};

// ============================================================================
// OptionValue type mapping
// ============================================================================

template<typename T> struct OptionValueType;
template<Parsable T> struct OptionValueType<Option<T>>    { using type = std::optional<T>; };
template<>           struct OptionValueType<Flag>          { using type = bool; };
template<Parsable T> struct OptionValueType<Positional<T>> { using type = std::optional<T>; };

template<typename T>
using OptionValue = typename OptionValueType<std::decay_t<T>>::type;

// ============================================================================
// OptionTable
// ============================================================================

template<typename... Entries>
struct OptionTable {
    std::tuple<Entries...> entries;

    constexpr OptionTable(Entries... args) noexcept
        : entries(std::move(args)...) {}

    consteval void validate() const noexcept {
        check_unique_names(std::index_sequence_for<Entries...>{});
    }

private:
    template<size_t... Is>
    consteval void check_unique_names(std::index_sequence<Is...>) const noexcept {
        constexpr auto long_names  = std::array{ std::get<Is>(entries).long_name... };
        constexpr auto short_names = std::array{ std::get<Is>(entries).short_name... };
        for (size_t i = 0; i < long_names.size(); ++i)
            for (size_t j = i + 1; j < long_names.size(); ++j) {
                if (!long_names[i].empty() && long_names[i] == long_names[j])
                    throw;  // compile error: duplicate long_name
                if (short_names[i] != '\0' && short_names[j] != '\0'
                    && short_names[i] == short_names[j])
                    throw;  // compile error: duplicate short_name
            }
    }
};

template<typename... Entries>
OptionTable(Entries...) -> OptionTable<Entries...>;

// ============================================================================
// ParseResult
// ============================================================================

template<typename... Entries>
struct ParseResult {
    std::tuple<OptionValue<Entries>...> value;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> messages;

    explicit operator bool() const noexcept { return diagnostics.empty(); }
};

} // namespace cli
