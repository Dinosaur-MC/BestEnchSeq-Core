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
int find_command_by_name(const std::tuple<Entries...>& tup, std::string_view name) noexcept {
    int r = -1;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]() -> bool {
            const auto& e = std::get<Is>(tup);
            if constexpr (is_command<std::decay_t<decltype(e)>>::value)
                if (e.name == name) { r = static_cast<int>(Is); return false; }
            return true;
        }()) && ...);
    }(std::index_sequence_for<Entries...>{});
    return r;
}

template<typename... Entries>
bool has_commands(const std::tuple<Entries...>& tup) noexcept {
    bool r = false;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (is_command<ET>::value) r = true;
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
    return r;
}

template<typename... Entries>
void set_value_by_index(std::tuple<OptionValue<Entries>...>& tup, size_t index,
                        std::string_view val, std::vector<Diagnostic>& diags,
                        std::string_view opt) {
    // Not noexcept: the std::string overload of from_string() allocates, and a
    // bad_alloc must propagate to the caller as an exception instead of
    // terminating inside a noexcept frame.
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((index == Is ? [&]() -> bool {
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<ET, Flag>) { std::get<Is>(tup) = true; }
            else if constexpr (!is_command<ET>::value) {
                using VT = typename OptionValueType<ET>::type::value_type;
                VT p{};
                if (from_string(val, p)) std::get<Is>(tup) = std::move(p);
                else diags.push_back(Diagnostic{ParseErrorCode::invalid_value, val, std::string(opt)});
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
            if constexpr (requires { v.has_value(); }) { if (!v.has_value()) d.push_back(Diagnostic{ParseErrorCode::required_missing, {}, std::string(std::get<Is>(e).long_name)}); }
        }}}()), ...);
    }(std::index_sequence_for<Entries...>{});
}

/// 共享 token 循环：顶层与嵌套命令层复用。
/// tokens: 本层待解析 token（顶层不含 argv[0]）；is_sub_level: 是否位于某命令之下；
/// parent_path: 父层完整命令路径（顶层为空）；diag_trans: 本层诊断格式化器。
/// 返回最终完整命令路径；out.command_path 始终置为该路径；层内完成 defaults/required/messages。
template<typename... Entries>
std::vector<std::string_view> parse_tokens_impl(
    std::span<const char*> tokens,
    const OptionTable<Entries...>& table,
    ParseResult<Entries...>& out,
    bool is_sub_level,
    const std::vector<std::string_view>& parent_path,
    const std::function<std::string(const Diagnostic&)>& diag_trans) {
    auto& tup = out.value;
    const auto& entries = table.entries;
    const bool cmds = detail::has_commands(entries);
    out.command_path = parent_path;

    bool ended = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string_view a(tokens[i]);
        if (a == "--") { ended = true; continue; }

        // ── 非选项 token ──
        if (ended || !(a.size() >= 2 && a[0] == '-')) {
            // ① 命令匹配（首匹配优先，大小写敏感；-- 之后永不匹配命令）
            if (!ended && cmds) {
                const int cidx = detail::find_command_by_name(entries, a);
                if (cidx >= 0) {
                    std::vector<std::string_view> path = parent_path;
                    path.push_back(a);
                    out.command_path = path;
                    [&]<size_t... Is>(std::index_sequence<Is...>) {
                        ((cidx == static_cast<int>(Is) ? [&]() -> bool {
                            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                            if constexpr (is_command<ET>::value) {
                                using R = typename command_entries<ET>::result_type;
                                R sub;
                                out.command_path = detail::parse_tokens_impl(
                                    tokens.subspan(i + 1), std::get<Is>(entries).table, sub, true, path, diag_trans);
                                std::get<Is>(tup) = std::move(sub);
                            }
                            return true;
                        }() : false) || ...);
                    }(std::index_sequence_for<Entries...>{});
                    return out.command_path;
                }
            }
            // ② 位置参数
            bool ok = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((!ok && [&]() -> bool {
                    using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                    if constexpr (requires { typename ET::value_type; }) {
                        if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) {
                            if (!std::get<Is>(tup).has_value()) {
                                typename ET::value_type p{};
                                if (from_string(a, p)) { std::get<Is>(tup) = std::move(p); ok = true; }
                            }
                        }
                    }
                    return false;
                }()) || ...);
            }(std::index_sequence_for<Entries...>{});
            // ③ 无处可去：有命令的表报 unknown_command（-- 之后永不匹配命令，保持 unexpected_positional）
            if (!ok) {
                if (cmds && !ended)
                    out.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_command, a, {}});
                else
                    out.diagnostics.push_back(Diagnostic{ParseErrorCode::unexpected_positional, a, {}});
            }
            continue;
        }

        // ── 长选项（原 parse() 第 178–226 行搬移；args → tokens，r.diagnostics → out.diagnostics）──
        if (a[1] == '-') {
            auto eq = a.find('=', 2);
            std::string_view key, val;
            bool h = eq != std::string_view::npos;
            if (h) { key = a.substr(2, eq - 2); val = a.substr(eq + 1); } else key = a.substr(2);
            int idx = detail::find_by_long_name(entries, key);
            if (idx < 0) { out.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, std::string(key)}); continue; }
            if (detail::is_flag_by_index(entries, idx)) {
                if (h)
                    out.diagnostics.push_back(Diagnostic{ParseErrorCode::flag_takes_no_value, a, std::string(key)});
                else
                    detail::set_value_by_index<Entries...>(tup, idx, {}, out.diagnostics, key);
            } else {
                bool already = false;
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    ([&]{
                        using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                        if constexpr (!std::is_same_v<ET, Flag>) {
                            if (idx == static_cast<int>(Is) && std::get<Is>(tup).has_value())
                                already = true;
                        }
                    }(), ...);
                }(std::index_sequence_for<Entries...>{});
                if (already)
                    out.diagnostics.push_back(Diagnostic{ParseErrorCode::duplicate_option, a, std::string(key)});
                if (h) {
                    if (val.empty())
                        out.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string(key)});
                    else
                        detail::set_value_by_index<Entries...>(tup, idx, val, out.diagnostics, key);
                } else if (i + 1 < tokens.size()) {
                    std::string_view n(tokens[i + 1]);
                    if (!n.empty() && !(n.size() >= 2 && n[0] == '-')) { ++i; detail::set_value_by_index<Entries...>(tup, idx, n, out.diagnostics, key); }
                    else out.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string(key)});
                } else out.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string(key)});
            }
            continue;
        }

        // ── 短选项（原 parse() 第 228–276 行搬移；args → tokens，r.diagnostics → out.diagnostics）──
        std::string_view sc = a.substr(1);
        if (sc.size() > 1) {
            bool all = true;
            for (char c : sc) { int ix = detail::find_by_short_name(entries, c); if (ix < 0 || !detail::is_flag_by_index(entries, ix)) { all = false; break; } }
            if (all) {
                for (char c : sc) {
                    int ix = detail::find_by_short_name(entries, c);
                    if (ix >= 0) detail::set_value_by_index<Entries...>(tup, ix, {}, out.diagnostics, {&c, 1});
                    else out.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, std::string(&c, 1)});
                }
                continue;
            }
        }
        char f = sc[0];
        int idx = detail::find_by_short_name(entries, f);
        if (idx < 0) { out.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_option, a, std::string(&f, 1)}); continue; }
        if (detail::is_flag_by_index(entries, idx)) {
            if (sc.size() > 1)
                out.diagnostics.push_back(Diagnostic{ParseErrorCode::flag_takes_no_value, a, std::string(&f, 1)});
            else
                detail::set_value_by_index<Entries...>(tup, idx, {}, out.diagnostics, {&f, 1});
        } else {
            bool already = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&]{
                    using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                    if constexpr (!std::is_same_v<ET, Flag>) {
                        if (idx == static_cast<int>(Is) && std::get<Is>(tup).has_value())
                            already = true;
                    }
                }(), ...);
            }(std::index_sequence_for<Entries...>{});
            if (already)
                out.diagnostics.push_back(Diagnostic{ParseErrorCode::duplicate_option, a, std::string(&f, 1)});
            if (sc.size() > 1) detail::set_value_by_index<Entries...>(tup, idx, sc.substr(1), out.diagnostics, {&f, 1});
            else if (i + 1 < tokens.size()) {
                std::string_view n(tokens[i + 1]);
                if (!n.empty() && !(n.size() >= 2 && n[0] == '-')) { ++i; detail::set_value_by_index<Entries...>(tup, idx, n, out.diagnostics, std::string_view(&f, 1)); }
                else out.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string(&f, 1)});
            } else out.diagnostics.push_back(Diagnostic{ParseErrorCode::missing_value, a, std::string(&f, 1)});
        }
    }

    detail::apply_defaults(tup, entries);
    detail::check_required(tup, entries, out.diagnostics);
    out.messages.reserve(out.diagnostics.size());
    for (auto& d : out.diagnostics) out.messages.push_back(diag_trans(d));
    return out.command_path;
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
    const std::span<const char*> tokens = args.size() > 0 ? args.subspan(1) : args;
    detail::parse_tokens_impl(tokens, _table, r, false, {}, _diag_trans);
    return r;
}

template<typename... Entries>
std::string CLIParser<Entries...>::format_help(std::string_view prog) const {
    std::string r;
    r += "Usage: "; r += prog; r += " [options]";
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{ using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (std::is_same_v<ET, Positional<typename ET::value_type>>) {
                r += " <"; r += std::get<Is>(_table.entries).name; r += '>';
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
    r += "\n\n";

    // ── 1. Collect unique group names in encounter order ──
    struct GroupEntry { std::string_view name; size_t first_idx; };
    auto groups = [&]() {
        std::vector<GroupEntry> gs;
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([&]{
                const auto& e = std::get<Is>(_table.entries);
                std::string_view g;
                if constexpr (requires { e.help_group; }) g = e.help_group;
                if (g.empty()) return;
                for (auto& grp : gs) { if (grp.name == g) return; }
                gs.push_back({g, Is});
            }()), ...);
        }(std::index_sequence_for<Entries...>{});
        return gs;
    }();

    // ── 2. Render groups in order ──
    for (const auto& [gname, _] : groups) {
        r += "  --- "; r += _help_trans(gname); r += " ---\n";
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([&]{
                const auto& e = std::get<Is>(_table.entries);
                using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                std::string_view eg;
                if constexpr (requires { e.help_group; }) eg = e.help_group;
                if (eg != gname) return;
                std::string l = "  ";
                if constexpr (requires { e.short_name; }) {
                    if (e.short_name != '\0') { l += '-'; l += e.short_name; l += ", "; }
                    else l += "    ";
                }
                if constexpr (requires { e.long_name; })
                    if (!e.long_name.empty()) { l += "--"; l += e.long_name;
                        if constexpr (!std::is_same_v<ET, Flag>) l += " <value>"; }
                while (l.size() < 28) l += ' ';
                l += _help_trans(e.help_key);
                if constexpr (!std::is_same_v<ET, Flag>)
                    if constexpr (requires { e.default_v; })
                        if (e.default_v.has_value()) {
                            l += " (default: ";
                            if constexpr (std::is_same_v<typename ET::value_type, std::string>)
                                l += *e.default_v;
                            else if constexpr (std::is_same_v<typename ET::value_type, int>)
                                l += std::to_string(*e.default_v);
                            else l += "set";
                            l += ')';
                        }
                if constexpr (requires { e.required; }) if (e.required) l += " (required)";
                l += '\n'; r += l;
            }()), ...);
        }(std::index_sequence_for<Entries...>{});
        r += '\n';
    }

    // ── 3. Ungrouped options (no help_group set) ──
    // Render these after all named groups without a header.
    bool has_ungrouped = false;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]{
            const auto& e = std::get<Is>(_table.entries);
            std::string_view eg;
            if constexpr (requires { e.help_group; }) eg = e.help_group;
            if (eg.empty()) has_ungrouped = true;
        }()), ...);
    }(std::index_sequence_for<Entries...>{});

    if (has_ungrouped) {
        if (!groups.empty()) r += '\n';
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([&]{
                const auto& e = std::get<Is>(_table.entries);
                using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
                std::string_view eg;
                if constexpr (requires { e.help_group; }) eg = e.help_group;
                if (!eg.empty()) return;  // only render ungrouped entries
                std::string l = "  ";
                if constexpr (requires { e.short_name; }) {
                    if (e.short_name != '\0') { l += '-'; l += e.short_name; l += ", "; }
                    else l += "    ";
                }
                if constexpr (requires { e.long_name; })
                    if (!e.long_name.empty()) { l += "--"; l += e.long_name;
                        if constexpr (!std::is_same_v<ET, Flag>) l += " <value>"; }
                while (l.size() < 28) l += ' ';
                l += _help_trans(e.help_key);
                if constexpr (!std::is_same_v<ET, Flag>)
                    if constexpr (requires { e.default_v; })
                        if (e.default_v.has_value()) {
                            l += " (default: ";
                            if constexpr (std::is_same_v<typename ET::value_type, std::string>)
                                l += *e.default_v;
                            else if constexpr (std::is_same_v<typename ET::value_type, int>)
                                l += std::to_string(*e.default_v);
                            else l += "set";
                            l += ')';
                        }
                if constexpr (requires { e.required; }) if (e.required) l += " (required)";
                l += '\n'; r += l;
            }()), ...);
        }(std::index_sequence_for<Entries...>{});
    }

    return r;
}

} // namespace cli
