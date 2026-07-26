// src/common/utils/cli/CLIParser.h
#pragma once

#include "CLICommon.h"
#include "CLIFormatter.h"
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// ============================================================================
// Entry Types -- declare your CLI options with these
// ============================================================================

template<Parsable T>
struct Option {
    using value_type = T;
    std::string_view  long_name;         // "--target"
    char              short_name = '\0'; // 't', '\0' = none
    std::string_view  help_key;          // translation key or fallback English text
    std::optional<T>  default_v;
    bool              required = false;
};

struct Flag {
    using value_type = bool;
    std::string_view  long_name;
    char              short_name = '\0';
    std::string_view  help_key;
};

template<Parsable T>
struct Positional {
    using value_type = T;
    std::string_view  name;              // display name in help text
    std::string_view  help_key;
    std::optional<T>  default_v;         // has default => not required
};

// ============================================================================
// OptionTable -- constexpr collection of all entries
// ============================================================================

namespace detail {

// Helper: get long_name from any entry type (SFINAE-safe)
inline constexpr std::string_view get_long_name(const auto& entry) noexcept {
    if constexpr (requires { entry.long_name; })
        return entry.long_name;
    else
        return {};
}
inline constexpr char get_short_name(const auto& entry) noexcept {
    if constexpr (requires { entry.short_name; })
        return entry.short_name;
    else
        return '\0';
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

// ============================================================================
// Runtime dispatch helpers -- fold-expression-based index lookup
// ============================================================================

template<typename... Entries, size_t... Is>
bool is_flag_impl(const std::tuple<Entries...>&, size_t index, std::index_sequence<Is...>) noexcept {
    bool result = false;
    ([&]{
        if (index == Is) {
            result = std::is_same_v<std::tuple_element_t<Is, std::tuple<Entries...>>, Flag>;
        }
    }(), ...);
    return result;
}

template<typename... Entries>
bool is_flag_by_index(const std::tuple<Entries...>& entries, size_t index) noexcept {
    return is_flag_impl<Entries...>(entries, index, std::index_sequence_for<Entries...>{});
}

template<typename... Entries, size_t... Is>
std::string_view get_entry_long_name_impl(const std::tuple<Entries...>& entries, size_t index, std::index_sequence<Is...>) noexcept {
    std::string_view result;
    (([&]{
        if (index == Is) {
            const auto& entry = std::get<Is>(entries);
            if constexpr (requires { entry.long_name; })
                result = entry.long_name;
        }
    }()), ...);
    return result;
}

template<typename... Entries>
std::string_view get_entry_long_name(const std::tuple<Entries...>& entries, size_t index) noexcept {
    return get_entry_long_name_impl<Entries...>(entries, index, std::index_sequence_for<Entries...>{});
}

template<typename... Entries>
int find_by_long_name(const std::tuple<Entries...>& tup, std::string_view name) noexcept {
    int result = -1;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]() -> bool {
            const auto& entry = std::get<Is>(tup);
            if constexpr (requires { entry.long_name; }) {
                if (entry.long_name == name) {
                    result = static_cast<int>(Is);
                    return false;
                }
            }
            return true;
        }()) && ...);
    }(std::index_sequence_for<Entries...>{});
    return result;
}

template<typename... Entries>
int find_by_short_name(const std::tuple<Entries...>& tup, char name) noexcept {
    if (name == '\0') return -1;
    int result = -1;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]() -> bool {
            const auto& entry = std::get<Is>(tup);
            if constexpr (requires { entry.short_name; }) {
                if (entry.short_name == name) {
                    result = static_cast<int>(Is);
                    return false;
                }
            }
            return true;
        }()) && ...);
    }(std::index_sequence_for<Entries...>{});
    return result;
}

// Set value by runtime index -- dispatch through fold expression
template<typename... Entries>
void set_value_by_index(std::tuple<OptionValue<Entries>...>& tup,
                        size_t index,
                        std::string_view val_str,
                        std::vector<Diagnostic>& diags,
                        std::string_view option_name) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((index == Is ? [&]() -> bool {
            using EntryT = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<EntryT, Flag>) {
                std::get<Is>(tup) = true;
            } else {
                using ValT = typename OptionValueType<EntryT>::type::value_type;
                ValT parsed{};
                if (from_string(val_str, parsed)) {
                    std::get<Is>(tup) = std::move(parsed);
                } else {
                    diags.push_back(Diagnostic{ParseErrorCode::invalid_value, val_str, option_name});
                }
            }
            return true;
        }() : false) || ...);
    }(std::index_sequence_for<Entries...>{});
}

// Apply defaults for unset non-flag options
template<typename... Entries>
void apply_defaults(std::tuple<OptionValue<Entries>...>& tup,
                    const std::tuple<Entries...>& entries) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            using EntryT = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (!std::is_same_v<EntryT, Flag>) {
                auto& val = std::get<Is>(tup);
                if (!val.has_value()) {
                    const auto& entry = std::get<Is>(entries);
                    if constexpr (requires { entry.default_v; }) {
                        if (entry.default_v.has_value()) {
                            val = *entry.default_v;
                        }
                    }
                }
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
}

// Check required options
template<typename... Entries>
void check_required(const std::tuple<OptionValue<Entries>...>& tup,
                    const std::tuple<Entries...>& entries,
                    std::vector<Diagnostic>& diags) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            if constexpr (requires { std::get<Is>(entries).required; }) {
                if (std::get<Is>(entries).required) {
                    const auto& val = std::get<Is>(tup);
                    if constexpr (requires { val.has_value(); }) {
                        if (!val.has_value()) {
                            diags.push_back(Diagnostic{
                                ParseErrorCode::required_missing,
                                {},
                                std::get<Is>(entries).long_name
                            });
                        }
                    }
                }
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
}

// Compile-time generated: for each entry index, a setter function pointer
template<typename... Entries>
using SetterFn = void (*)(std::tuple<OptionValue<Entries>...>&, std::string_view);

} // namespace detail

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
        constexpr auto long_names = std::array{ std::get<Is>(entries).long_name... };
        constexpr auto short_names = std::array{ std::get<Is>(entries).short_name... };
        for (size_t i = 0; i < long_names.size(); ++i) {
            for (size_t j = i + 1; j < long_names.size(); ++j) {
                if (!long_names[i].empty() && long_names[i] == long_names[j])
                    throw;  // compile error in consteval context: duplicate long_name
                if (short_names[i] != '\0' && short_names[j] != '\0' && short_names[i] == short_names[j])
                    throw;  // compile error: duplicate short_name
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
    std::vector<std::string> messages;  // human-readable translations of diagnostics

    explicit operator bool() const noexcept { return diagnostics.empty(); }
};

// SFINAE helper: construct diagnostic translator from T if callable, else English
namespace detail {
inline auto make_diag_trans(auto& t) -> std::function<std::string(const Diagnostic&)> {
    if constexpr (requires { t(std::declval<const Diagnostic&>()); })
        return std::function<std::string(const Diagnostic&)>(t);
    else
        return DefaultDiagnosticFormatter{};
}
} // namespace detail

// ============================================================================
// CLIParser — type-safe, i18n-capable CLI argument parser
// ============================================================================
//
// Translation is handled entirely inside. Provide a translator at construction;
// parse() returns ParseResult with both raw Diagnostics and translated messages.
//
// Usage:
//   CLIParser parser(table, my_translator);   // see UnifiedDefaultFormatter
//   auto r = parser.parse(args);
//   for (auto& msg : r.messages) std::cerr << msg;
//   std::cout << parser.format_help("prog");

template<typename... Entries>
class CLIParser {
    OptionTable<Entries...> _table;
    std::function<std::string(std::string_view)>   _help_trans;
    std::function<std::string(const Diagnostic&)>  _diag_trans;

public:
    using result_type = ParseResult<Entries...>;

    constexpr CLIParser() noexcept = default;

    /// Construct with table only — English fallback.
    explicit constexpr CLIParser(OptionTable<Entries...> table) noexcept
        : _table(std::move(table))
        , _help_trans(UnifiedDefaultFormatter{})
        , _diag_trans(UnifiedDefaultFormatter{}) {}

    /// Construct with a unified translator.
    /// If it supports `const Diagnostic&`, used for both help and diagnostics.
    /// Otherwise, diagnostics fall back to English.
    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, OptionTable<Entries...>>)
    CLIParser(OptionTable<Entries...> table, T&& translator)
        : _table(std::move(table))
        , _help_trans(translator)
        , _diag_trans(detail::make_diag_trans(translator)) {}

    void set_help_translator(std::function<std::string(std::string_view)> t) noexcept {
        _help_trans = std::move(t);
    }

    void set_diagnostic_translator(std::function<std::string(const Diagnostic&)> t) noexcept {
        _diag_trans = std::move(t);
    }

    /// Parse arguments — returns result with translated messages filled.
    ParseResult<Entries...> parse(std::span<const char*> args) const;

    /// Generate localized help text.
    std::string format_help(std::string_view program_name) const;

    const OptionTable<Entries...>& table() const noexcept { return _table; }
};

// CTAD deduction guides
template<typename... Entries>
CLIParser(OptionTable<Entries...>) -> CLIParser<Entries...>;

template<typename... Entries, typename T>
CLIParser(OptionTable<Entries...>, T) -> CLIParser<Entries...>;

// ============================================================================
// CLIParser member implementations
// ============================================================================

template<typename... Entries>
ParseResult<Entries...> CLIParser<Entries...>::parse(std::span<const char*> args) const {
    ParseResult<Entries...> result;
    auto& tup = result.value;
    const auto& entries = _table.entries;

    size_t arg_start = (args.size() > 0) ? 1 : 0;
    bool options_ended = false;

    for (size_t i = arg_start; i < args.size(); ++i) {
        std::string_view arg(args[i]);

        // -- end-of-options marker
        if (arg == "--") {
            options_ended = true;
            continue;
        }

        // Positional argument (after -- or no - prefix)
        if (options_ended || !(arg.size() >= 2 && arg[0] == '-')) {
            bool assigned = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((!assigned && [&]() -> bool {
                    using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                    if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) {
                        if (!std::get<Is>(tup).has_value()) {
                            typename ET::value_type parsed{};
                            if (from_string(arg, parsed)) {
                                std::get<Is>(tup) = std::move(parsed);
                                assigned = true;
                            }
                        }
                    }
                    return false;
                }()) || ...);
            }(std::index_sequence_for<Entries...>{});
            if (!assigned) {
                result.diagnostics.push_back(Diagnostic{
                    ParseErrorCode::unexpected_positional, arg, {}
                });
            }
            continue;
        }

        // Long option: --key=value or --key value or --flag
        if (arg[1] == '-') {
            auto eq_pos = arg.find('=', 2);
            std::string_view key;
            std::string_view value;
            bool has_eq = eq_pos != std::string_view::npos;

            if (has_eq) {
                key = arg.substr(2, eq_pos - 2);
                value = arg.substr(eq_pos + 1);
            } else {
                key = arg.substr(2);
            }

            int idx = detail::find_by_long_name(entries, key);
            if (idx < 0) {
                result.diagnostics.push_back(Diagnostic{
                    ParseErrorCode::unknown_option, arg, key
                });
                continue;
            }

            if (detail::is_flag_by_index(entries, idx)) {
                detail::set_value_by_index<Entries...>(tup, idx, {}, result.diagnostics, key);
            } else {
                if (has_eq) {
                    detail::set_value_by_index<Entries...>(tup, idx, value, result.diagnostics, key);
                } else {
                    if (i + 1 < args.size()) {
                        std::string_view next(args[i + 1]);
                        if (!next.empty() && next[0] != '-') {
                            ++i;
                            detail::set_value_by_index<Entries...>(tup, idx, next, result.diagnostics, key);
                        } else {
                            result.diagnostics.push_back(Diagnostic{
                                ParseErrorCode::missing_value, arg, key
                            });
                        }
                    } else {
                        result.diagnostics.push_back(Diagnostic{
                            ParseErrorCode::missing_value, arg, key
                        });
                    }
                }
            }
            continue;
        }

        // Short option: -x, -abc
        std::string_view short_chars = arg.substr(1);

        // Try to expand -abc (only if all chars reference Flag entries)
        if (short_chars.size() > 1) {
            bool all_flags = true;
            for (char c : short_chars) {
                int idx = detail::find_by_short_name(entries, c);
                if (idx < 0 || !detail::is_flag_by_index(entries, idx)) {
                    all_flags = false;
                    break;
                }
            }
            if (all_flags) {
                for (char c : short_chars) {
                    int idx = detail::find_by_short_name(entries, c);
                    if (idx >= 0) {
                        detail::set_value_by_index<Entries...>(tup, idx, {}, result.diagnostics, {&c, 1});
                    } else {
                        result.diagnostics.push_back(Diagnostic{
                            ParseErrorCode::unknown_option, arg, std::string_view(&c, 1)
                        });
                    }
                }
                continue;
            }
        }

        // Single short option
        char first = short_chars[0];
        int idx = detail::find_by_short_name(entries, first);
        if (idx < 0) {
            result.diagnostics.push_back(Diagnostic{
                ParseErrorCode::unknown_option, arg, std::string_view(&first, 1)
            });
            continue;
        }

        if (detail::is_flag_by_index(entries, idx)) {
            detail::set_value_by_index<Entries...>(tup, idx, {}, result.diagnostics, {&first, 1});
        } else {
            if (short_chars.size() > 1) {
                detail::set_value_by_index<Entries...>(tup, idx, short_chars.substr(1), result.diagnostics, {&first, 1});
            } else if (i + 1 < args.size()) {
                std::string_view next(args[i + 1]);
                if (!next.empty() && next[0] != '-') {
                    ++i;
                    detail::set_value_by_index<Entries...>(tup, idx, next, result.diagnostics, std::string_view(&first, 1));
                } else {
                    result.diagnostics.push_back(Diagnostic{
                        ParseErrorCode::missing_value, arg, std::string_view(&first, 1)
                    });
                }
            } else {
                result.diagnostics.push_back(Diagnostic{
                    ParseErrorCode::missing_value, arg, std::string_view(&first, 1)
                });
            }
        }
    }

    // Apply defaults for unset values
    detail::apply_defaults(tup, entries);

    // Check required options
    detail::check_required(tup, entries, result.diagnostics);

    // Translate diagnostics to human-readable messages
    result.messages.reserve(result.diagnostics.size());
    for (auto& d : result.diagnostics)
        result.messages.push_back(_diag_trans(d));

    return result;
}

template<typename... Entries>
std::string CLIParser<Entries...>::format_help(std::string_view program_name) const {
    std::string result;
    result += "Usage: ";
    result += program_name;
    result += " [options]";

    // Append positional argument names
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) {
                const auto& entry = std::get<Is>(_table.entries);
                result += " <";
                result += entry.name;
                result += '>';
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});

    result += "\n\nOptions:\n";

    // List all options
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            const auto& entry = std::get<Is>(_table.entries);
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;

            std::string line = "  ";

            if constexpr (requires { entry.short_name; }) {
                if (entry.short_name != '\0') {
                    line += '-';
                    line += entry.short_name;
                    line += ", ";
                } else {
                    line += "    ";
                }
            }

            if constexpr (requires { entry.long_name; }) {
                if (!entry.long_name.empty()) {
                    line += "--";
                    line += entry.long_name;
                    if constexpr (!std::is_same_v<ET, Flag>) {
                        line += " <value>";
                    }
                }
            }

            while (line.size() < 28) line += ' ';

            line += _help_trans(entry.help_key);

            if constexpr (!std::is_same_v<ET, Flag>) {
                if constexpr (requires { entry.default_v; }) {
                    if (entry.default_v.has_value()) {
                        line += " (default: ";
                        if constexpr (std::is_same_v<typename ET::value_type, std::string>) {
                            line += *entry.default_v;
                        } else if constexpr (std::is_same_v<typename ET::value_type, int>) {
                            line += std::to_string(*entry.default_v);
                        } else {
                            line += "set";
                        }
                        line += ')';
                    }
                }
            }

            if constexpr (requires { entry.required; }) {
                if (entry.required) {
                    line += " (required)";
                }
            }

            line += '\n';
            result += line;
        }()), ...);
    }(std::index_sequence_for<Entries...>{});

    return result;
}
