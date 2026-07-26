// src/common/utils/cli/CLIParser.hpp
#pragma once

#include "CLICommon.h"
#include "CLIFormatter.h"
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace cli {

namespace detail {

// ── Runtime dispatch helpers ────────────────────────────────────────────

template<typename... Entries, size_t... Is>
bool is_flag_impl(const std::tuple<Entries...>&, size_t index, std::index_sequence<Is...>) noexcept {
    bool r = false;
    ([&]{ if (index == Is) r = std::is_same_v<std::tuple_element_t<Is, std::tuple<Entries...>>, Flag>; }(), ...);
    return r;
}

template<typename... Entries>
bool is_flag_by_index(const std::tuple<Entries...>& e, size_t idx) noexcept {
    return is_flag_impl<Entries...>(e, idx, std::index_sequence_for<Entries...>{});
}

template<typename... Entries>
int find_by_long_name(const std::tuple<Entries...>& tup, std::string_view name) noexcept {
    int r = -1;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]() -> bool {
            const auto& e = std::get<Is>(tup);
            if constexpr (requires { e.long_name; }) { if (e.long_name == name) { r = static_cast<int>(Is); return false; } }
            return true;
        }()) && ...);
    }(std::index_sequence_for<Entries...>{});
    return r;
}

template<typename... Entries>
int find_by_short_name(const std::tuple<Entries...>& tup, char name) noexcept {
    if (name == '\0') return -1;
    int r = -1;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]() -> bool {
            const auto& e = std::get<Is>(tup);
            if constexpr (requires { e.short_name; }) { if (e.short_name == name) { r = static_cast<int>(Is); return false; } }
            return true;
        }()) && ...);
    }(std::index_sequence_for<Entries...>{});
    return r;
}

template<typename... Entries>
void set_value_by_index(std::tuple<OptionValue<Entries>...>& tup, size_t index,
                        std::string_view val, std::vector<Diagnostic>& diags,
                        std::string_view opt) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((index == Is ? [&]() -> bool {
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<ET, Flag>) { std::get<Is>(tup) = true; }
            else {
                using VT = typename OptionValueType<ET>::type::value_type;
                VT p{};
                if (from_string(val, p)) std::get<Is>(tup) = std::move(p);
                else diags.push_back(Diagnostic{ParseErrorCode::invalid_value, val, opt});
            }
            return true;
        }() : false) || ...);
    }(std::index_sequence_for<Entries...>{});
}

template<typename... Entries>
void apply_defaults(std::tuple<OptionValue<Entries>...>& tup, const std::tuple<Entries...>& e) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{ using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (!std::is_same_v<ET, Flag>) {
                auto& v = std::get<Is>(tup);
                if (!v.has_value()) { const auto& en = std::get<Is>(e); if constexpr (requires { en.default_v; }) { if (en.default_v.has_value()) v = *en.default_v; } }
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
}

template<typename... Entries>
void check_required(const std::tuple<OptionValue<Entries>...>& tup, const std::tuple<Entries...>& e, std::vector<Diagnostic>& d) noexcept {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{ if constexpr (requires { std::get<Is>(e).required; }) { if (std::get<Is>(e).required) {
            const auto& v = std::get<Is>(tup);
            if constexpr (requires { v.has_value(); }) { if (!v.has_value()) d.push_back(Diagnostic{ParseErrorCode::required_missing, {}, std::get<Is>(e).long_name}); }
        }}}()), ...);
    }(std::index_sequence_for<Entries...>{});
}

inline auto make_diag_trans(auto& t) -> std::function<std::string(const Diagnostic&)> {
    if constexpr (requires { t(std::declval<const Diagnostic&>()); })
        return std::function<std::string(const Diagnostic&)>(t);
    else
        return DefaultDiagnosticFormatter{};
}

} // namespace detail

// ============================================================================
// CLIParser
// ============================================================================

template<typename... Entries>
class CLIParser {
    OptionTable<Entries...> _table;
    std::function<std::string(std::string_view)>   _help_trans;
    std::function<std::string(const Diagnostic&)>  _diag_trans;

public:
    using result_type = ParseResult<Entries...>;
    constexpr CLIParser() noexcept = default;
    explicit constexpr CLIParser(OptionTable<Entries...> table) noexcept
        : _table(std::move(table)), _help_trans(UnifiedDefaultFormatter{}), _diag_trans(UnifiedDefaultFormatter{}) {}

    template<typename T> requires (!std::is_same_v<std::decay_t<T>, OptionTable<Entries...>>)
    CLIParser(OptionTable<Entries...> table, T&& t)
        : _table(std::move(table)), _help_trans(t), _diag_trans(detail::make_diag_trans(t)) {}

    void set_help_translator(std::function<std::string(std::string_view)> t) noexcept { _help_trans = std::move(t); }
    void set_diagnostic_translator(std::function<std::string(const Diagnostic&)> t) noexcept { _diag_trans = std::move(t); }

    ParseResult<Entries...> parse(std::span<const char*> args) const;
    std::string format_help(std::string_view program_name) const;
    const OptionTable<Entries...>& table() const noexcept { return _table; }
};

template<typename... Entries> CLIParser(OptionTable<Entries...>) -> CLIParser<Entries...>;
template<typename... Entries, typename T> CLIParser(OptionTable<Entries...>, T) -> CLIParser<Entries...>;

// ============================================================================
// Member implementations
// ============================================================================

template<typename... Entries>
ParseResult<Entries...> CLIParser<Entries...>::parse(std::span<const char*> args) const {
    ParseResult<Entries...> r;
    auto& tup = r.value;
    const auto& entries = _table.entries;

    size_t start = (args.size() > 0) ? 1 : 0;
    bool ended = false;

    for (size_t i = start; i < args.size(); ++i) {
        std::string_view a(args[i]);
        if (a == "--") { ended = true; continue; }

        // Positional
        if (ended || !(a.size() >= 2 && a[0] == '-')) {
            bool ok = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((!ok && [&]() -> bool {
                    using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                    if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) {
                        if (!std::get<Is>(tup).has_value()) {
                            typename ET::value_type p{};
                            if (from_string(a, p)) { std::get<Is>(tup) = std::move(p); ok = true; }
                        }
                    }
                    return false;
                }()) || ...);
            }(std::index_sequence_for<Entries...>{});
            if (!ok) r.diagnostics.push_back(Diagnostic{ParseErrorCode::unexpected_positional, a, {}});
            continue;
        }

        // Long option
        if (a[1] == '-') {
            auto eq = a.find('=', 2);
            std::string_view key, val;
            bool h = eq != std::string_view::npos;
            if (h) { key = a.substr(2, eq - 2); val = a.substr(eq + 1); } else key = a.substr(2);
            int idx = detail::find_by_long_name(entries, key);
            if (idx < 0) { r.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, key}); continue; }
            if (detail::is_flag_by_index(entries, idx)) detail::set_value_by_index<Entries...>(tup, idx, {}, r.diagnostics, key);
            else if (h) detail::set_value_by_index<Entries...>(tup, idx, val, r.diagnostics, key);
            else if (i + 1 < args.size()) {
                std::string_view n(args[i + 1]);
                if (!n.empty() && n[0] != '-') { ++i; detail::set_value_by_index<Entries...>(tup, idx, n, r.diagnostics, key); }
                else r.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, key});
            } else r.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, key});
            continue;
        }

        // Short option
        std::string_view sc = a.substr(1);
        if (sc.size() > 1) {
            bool all = true;
            for (char c : sc) { int ix = detail::find_by_short_name(entries, c); if (ix < 0 || !detail::is_flag_by_index(entries, ix)) { all = false; break; } }
            if (all) {
                for (char c : sc) {
                    int ix = detail::find_by_short_name(entries, c);
                    if (ix >= 0) detail::set_value_by_index<Entries...>(tup, ix, {}, r.diagnostics, {&c, 1});
                    else r.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, std::string_view(&c, 1)});
                }
                continue;
            }
        }
        char f = sc[0];
        int idx = detail::find_by_short_name(entries, f);
        if (idx < 0) { r.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, std::string_view(&f, 1)}); continue; }
        if (detail::is_flag_by_index(entries, idx)) detail::set_value_by_index<Entries...>(tup, idx, {}, r.diagnostics, {&f, 1});
        else if (sc.size() > 1) detail::set_value_by_index<Entries...>(tup, idx, sc.substr(1), r.diagnostics, {&f, 1});
        else if (i + 1 < args.size()) {
            std::string_view n(args[i + 1]);
            if (!n.empty() && n[0] != '-') { ++i; detail::set_value_by_index<Entries...>(tup, idx, n, r.diagnostics, std::string_view(&f, 1)); }
            else r.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string_view(&f, 1)});
        } else r.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string_view(&f, 1)});
    }

    detail::apply_defaults(tup, entries);
    detail::check_required(tup, entries, r.diagnostics);
    r.messages.reserve(r.diagnostics.size());
    for (auto& d : r.diagnostics) r.messages.push_back(_diag_trans(d));
    return r;
}

template<typename... Entries>
std::string CLIParser<Entries...>::format_help(std::string_view prog) const {
    std::string r;
    r += "Usage: "; r += prog; r += " [options]";
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{ using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) { r += " <"; r += std::get<Is>(_table.entries).name; r += '>'; }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
    r += "\n\nOptions:\n";
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{ const auto& e = std::get<Is>(_table.entries); using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            std::string l = "  ";
            if constexpr (requires { e.short_name; }) { if (e.short_name != '\0') { l += '-'; l += e.short_name; l += ", "; } else l += "    "; }
            if constexpr (requires { e.long_name; }) { if (!e.long_name.empty()) { l += "--"; l += e.long_name; if constexpr (!std::is_same_v<ET, Flag>) l += " <value>"; } }
            while (l.size() < 28) l += ' ';
            l += _help_trans(e.help_key);
            if constexpr (!std::is_same_v<ET, Flag>) { if constexpr (requires { e.default_v; }) { if (e.default_v.has_value()) {
                l += " (default: ";
                if constexpr (std::is_same_v<typename ET::value_type, std::string>) l += *e.default_v;
                else if constexpr (std::is_same_v<typename ET::value_type, int>) l += std::to_string(*e.default_v);
                else l += "set";
                l += ')';
            }}}
            if constexpr (requires { e.required; }) { if (e.required) l += " (required)"; }
            l += '\n'; r += l;
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
    return r;
}

} // namespace cli
