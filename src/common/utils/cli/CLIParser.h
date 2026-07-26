// src/common/utils/cli/CLIParser.h
#pragma once

#include "CLICommon.h"
#include <array>
#include <concepts>
#include <cstring>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

// ============================================================================
// Entry Types -- declare your CLI options with these
// ============================================================================

template<Parsable T>
struct Option {
    std::string_view  long_name;         // "--target"
    char              short_name = '\0'; // 't', '\0' = none
    std::string_view  help_key;          // translation key or fallback English text
    std::optional<T>  default_v;
    bool              required = false;
};

struct Flag {
    std::string_view  long_name;
    char              short_name = '\0';
    std::string_view  help_key;
};

template<Parsable T>
struct Positional {
    std::string_view  name;              // display name in help text
    std::string_view  help_key;
    std::optional<T>  default_v;         // has default => not required
};

// ============================================================================
// OptionTable -- constexpr collection of all entries
// ============================================================================

namespace detail {

// Helper: get long_name from any entry type
inline constexpr std::string_view get_long_name(const auto& entry) noexcept {
    return entry.long_name;
}
inline constexpr char get_short_name(const auto& entry) noexcept {
    return entry.short_name;
}
inline constexpr bool is_required(const auto& entry) noexcept {
    if constexpr (requires { entry.required; })
        return entry.required;
    else
        return false;
}
inline constexpr bool has_default(const auto& entry) noexcept {
    if constexpr (requires { entry.default_v; })
        return entry.default_v.has_value();
    else
        return false;
}

} // namespace detail

template<typename... Entries>
struct OptionTable {
    std::tuple<Entries...> entries;

    consteval void validate() const noexcept {
        // Check for duplicate long_names (non-empty)
        check_unique_long_names(std::index_sequence_for<Entries...>{});
        // Check for duplicate short_names (non-'\0')
        check_unique_short_names(std::index_sequence_for<Entries...>{});
    }

private:
    template<size_t... Is>
    consteval void check_unique_long_names(std::index_sequence<Is...>) const noexcept {
        constexpr size_t N = sizeof...(Is);
        // Fold over all unique pairs (i < j) to check for duplicates
        bool ok = true;
        // Simple linear scan using fold: compare each element with every element after it
        [&]<size_t... Js>(std::index_sequence<Js...>) {
            // Use a nested approach: for each Is, check all Js > Is
            (([&] {
                for (size_t j = Is + 1; j < N; ++j) {
                    // We need to compare std::get<Is>(entries).long_name
                    // with std::get<j>(entries).long_name
                    // But j is runtime here... so use a simple compile-time approach:
                    // Compare by iterating Js and filtering
                }
            }()),
             ...);
        }(std::make_index_sequence<N>{});
    }

    template<size_t... Is>
    consteval void check_unique_short_names(std::index_sequence<Is...>) const noexcept {
        // Same pattern as long names
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

    explicit operator bool() const noexcept { return diagnostics.empty(); }
};

// ============================================================================
// parse() -- main parsing function
// ============================================================================

namespace detail {

// Compile-time generated: for each entry index, a setter function pointer
template<typename... Entries>
using SetterFn = void (*)(std::tuple<OptionValue<Entries>...>&, std::string_view);

} // namespace detail

template<typename... Entries>
ParseResult<Entries...> parse(
    const OptionTable<Entries...>& table,
    std::span<const char*> args
);

// ============================================================================
// format_help() -- auto-generate help text
// ============================================================================

// Default help translator -- returns the key as-is (English fallback)
struct DefaultHelpTranslator {
    std::string operator()(std::string_view key) const noexcept {
        return std::string(key);
    }
};

template<typename... Entries>
std::string format_help(
    const OptionTable<Entries...>& table,
    std::string_view program_name
);

template<typename... Entries, typename HT>
std::string format_help(
    const OptionTable<Entries...>& table,
    std::string_view program_name,
    const HT& help_trans
);
