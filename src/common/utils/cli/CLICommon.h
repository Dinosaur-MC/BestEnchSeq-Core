// src/common/utils/cli/CLICommon.h
#pragma once

#include <array>
#include <cstdint>
#include <concepts>
#include <cstdio>
#include <exception>
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

    // Duplicate-name validation runs here, not at compile time: a compile-time
    // check would need the whole table to be a constant expression, but the
    // tables carry `std::string` defaults (non-literal in C++20), so they are
    // dynamically initialized.  validate() used to be consteval and never
    // CALLED — a duplicate long_name/short_name compiled silently and the
    // shadowed entry was unreachable.  It is invoked now, so a duplicate aborts
    // at process startup (tables are built once) instead of silently shadowing.
    constexpr OptionTable(Entries... args) noexcept
        : entries(std::move(args)...) {
        validate();
    }

    constexpr void validate() const noexcept {
        check_unique_names(std::index_sequence_for<Entries...>{});
    }

private:
    /// Uniform accessors so check_unique_names can build homogeneous arrays
    /// over mixed entry types: Positional carries neither long_name nor
    /// short_name (it contributes "" / '\0'), Flag/Option carry both.
    static constexpr std::string_view entry_long(const auto& e) noexcept {
        if constexpr (requires { e.long_name; }) return e.long_name;
        else return {};
    }
    static constexpr char entry_short(const auto& e) noexcept {
        if constexpr (requires { e.short_name; }) return e.short_name;
        else return '\0';
    }

    template<size_t... Is>
    constexpr void check_unique_names(std::index_sequence<Is...>) const noexcept {
        const auto long_names  = std::array{ entry_long(std::get<Is>(entries))... };
        const auto short_names = std::array{ entry_short(std::get<Is>(entries))... };
        for (size_t i = 0; i < long_names.size(); ++i)
            for (size_t j = i + 1; j < long_names.size(); ++j) {
                // Unreachable in a correct table.  Runs at process startup (the
                // tables are dynamically initialized — std::string defaults rule
                // out a compile-time check), so a duplicate aborts the process
                // with a message instead of silently shadowing an option.  The
                // build's own test binaries construct these tables, so a newly
                // introduced duplicate fails their startup immediately.
                if (!long_names[i].empty() && long_names[i] == long_names[j]) {
                    std::fprintf(stderr, "OptionTable: duplicate long_name '%.*s'\n",
                                 static_cast<int>(long_names[i].size()), long_names[i].data());
                    std::terminate();
                }
                if (short_names[i] != '\0' && short_names[j] != '\0'
                    && short_names[i] == short_names[j]) {
                    std::fprintf(stderr, "OptionTable: duplicate short_name '%c'\n", short_names[i]);
                    std::terminate();
                }
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
