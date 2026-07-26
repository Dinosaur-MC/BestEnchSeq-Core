#pragma once
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

/// Lightweight translation table. Immutable after construction.
class Language {
  public:
    using Table = std::unordered_map<std::string, std::string>;

    Language(std::string name, Table table);

    /// Language code, e.g. "zh_CN", "en_US".
    const std::string& name() const noexcept { return _name; }

    /// Look up `key` -> localized string.
    /// Returns `key` itself if not found (graceful fallback).
    std::string_view get(std::string_view key) const noexcept;

    /// Look up `key` and substitute {0} {1} ... positional placeholders.
    template <typename... Args>
    std::string format(std::string_view key, Args&&... args) const;

    /// Get all key-value pairs under a module prefix (e.g. "cli.help").
    std::vector<std::pair<std::string_view, std::string_view>>
    get_section(std::string_view prefix) const;

  private:
    std::string _name;
    Table _table;

    static std::string substitute_impl(
        std::string_view pattern,
        const std::vector<std::string>& args);
};

// ---- Helpers for Language::format ------------------------------------

namespace detail {

/// Convert a single argument to std::string.
/// Arithmetic types (int, float, etc.) use std::to_string.
/// Everything else must be constructible as std::string.
template <typename T>
inline std::string to_format_string(T&& val) {
    if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>>) {
        return std::to_string(std::forward<T>(val));
    } else {
        return std::string(std::forward<T>(val));
    }
}

} // namespace detail

// ---- Language::format (out-of-class) ---------------------------------

template <typename... Args>
inline std::string Language::format(std::string_view key, Args&&... args) const {
    auto pattern = get(key);
    std::vector<std::string> arg_vec;
    arg_vec.reserve(sizeof...(Args));
    (arg_vec.push_back(detail::to_format_string(std::forward<Args>(args))), ...);
    return substitute_impl(pattern, arg_vec);
}

// ---- LanguageManager -------------------------------------------------

class LanguageManager {
  public:
    static LanguageManager& instance();

    void register_language(Language lang);
    bool select(std::string_view code);

    const Language& active() const noexcept;
    std::vector<std::string> available() const;

    /// Match a POSIX locale string to the best available language.
    /// 1) exact match, 2) language-prefix match, 3) "en_US" fallback.
    std::string resolve_locale(std::string_view locale) const;

  private:
    LanguageManager() = default;
    LanguageManager(const LanguageManager&) = delete;
    LanguageManager& operator=(const LanguageManager&) = delete;
    LanguageManager(LanguageManager&&) = delete;
    LanguageManager& operator=(LanguageManager&&) = delete;
    std::unordered_map<std::string, Language> _langs;
    const Language* _active = nullptr;
};

// ---- Convenience free functions --------------------------------------

inline std::string tr(std::string_view key) {
    return std::string(LanguageManager::instance().active().get(key));
}

template <typename... Args>
inline std::string tr_fmt(std::string_view key, Args&&... args) {
    return LanguageManager::instance().active().format(key, std::forward<Args>(args)...);
}
