#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace detail {

struct TransparentHash {
    using is_transparent = void;
    using hash_type      = std::hash<std::string_view>;
    size_t operator()(std::string_view sv) const noexcept { return hash_type{}(sv); }
};

struct TransparentEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};
} // namespace

/// Lightweight translation table. Immutable after construction.
class Language {
  public:
    using Table = std::unordered_map<std::string, std::string, detail::TransparentHash, detail::TransparentEqual>;

    Language(std::string_view name, Table table);

    /// Language code, e.g. "zh_CN", "en_US".
    const std::string &name() const noexcept { return _name; }

    /// Look up `key` -> localized string.
    /// Returns `key` itself if not found (graceful fallback).
    std::string_view get(std::string_view key) const noexcept;

    /// Look up `key` and substitute {0} {1} ... positional placeholders.
    template <typename... Args> std::string format(std::string_view key, Args &&...args) const;

    /// Get all key-value pairs under a module prefix (e.g. "cli.help").
    std::vector<std::pair<std::string_view, std::string_view>> get_section(std::string_view prefix) const;

    /// Merge another Language's translation table into this one.
    /// Keys in `other` override existing keys.
    void merge(const Language& other);

  private:
    std::string _name;
    Table _table;

    static std::string substitute_impl(std::string_view pattern, const std::vector<std::string> &args);
};

// ---- Helpers for Language::format ------------------------------------

namespace detail {

/// Convert a single argument to std::string.
/// Arithmetic types (int, float, etc.) use std::to_string.
/// Everything else must be constructible as std::string.
template <typename T> inline std::string to_format_string(T &&val) {
    if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>>) {
        return std::to_string(std::forward<T>(val));
    } else {
        return std::string(std::forward<T>(val));
    }
}

} // namespace detail

// ---- Language::format (out-of-class) ---------------------------------

template <typename... Args> inline std::string Language::format(std::string_view key, Args &&...args) const {
    auto pattern = get(key);
    std::vector<std::string> arg_vec;
    arg_vec.reserve(sizeof...(Args));
    (arg_vec.push_back(detail::to_format_string(std::forward<Args>(args))), ...);
    return substitute_impl(pattern, arg_vec);
}

// ---- LanguageManager -------------------------------------------------

class LanguageManager {
  public:
    static LanguageManager &instance();

    void register_language(Language lang);

    /// Select a language by code.
    ///
    /// Resolution order:
    ///   1. Already registered → activate directly.
    ///   2. On-disk file `{langs_dir}/{code}.json` exists → load & activate.
    ///   3. Not found → fallback to en_US (keep current active if available).
    ///
    /// Returns true when the requested language is now active (including after
    /// a successful on-disk load). Returns false when falling back to en_US.
    bool select(std::string_view code);

    const Language &active() const noexcept;
    std::vector<std::string> available() const;

    /// Match a POSIX locale string to the best available language.
    /// 1) exact match, 2) language-prefix match, 3) "en_US" fallback.
    std::string resolve_locale(std::string_view locale) const;

    /// Set a directory to search for on-demand language file loading.
    /// Directory should contain `{code}.json` files matching the i18n format:
    ///   { "language": "zh_CN", "strings": { "key": "value", ... } }
    void set_langs_dir(std::filesystem::path dir);

    /// Attempt to load a language from `{langs_dir}/{code}.json`.
    /// Returns true if the file was found, parsed and registered.
    bool load_language(std::string_view code);

  private:
    LanguageManager()                                   = default;
    LanguageManager(const LanguageManager &)            = delete;
    LanguageManager &operator=(const LanguageManager &) = delete;
    LanguageManager(LanguageManager &&)                 = delete;
    LanguageManager &operator=(LanguageManager &&)      = delete;

    bool load_language_from_disk(std::string_view code);

    std::unordered_map<std::string, Language, detail::TransparentHash, detail::TransparentEqual> _langs;
    std::filesystem::path _langs_dir;
    const Language *_active = nullptr;
};

// ---- Convenience free functions --------------------------------------

inline std::string tr(std::string_view key) {
    return std::string(LanguageManager::instance().active().get(key));
}

template <typename... Args> inline std::string tr_fmt(std::string_view key, Args &&...args) {
    return LanguageManager::instance().active().format(key, std::forward<Args>(args)...);
}
