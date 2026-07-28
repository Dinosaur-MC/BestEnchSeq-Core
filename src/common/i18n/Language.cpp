#include "Language.h"
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace {
constexpr std::string_view kDefaultLang = "en_US";

// Normalize a language code: lowercase, '-' → '_'.
std::string normalize(std::string_view code) {
    std::string n(code);
    for (auto &c : n) {
        if (c == '-')
            c = '_';
        else
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return n;
}
} // namespace

Language::Language(std::string_view name, Table table) : _name(name), _table(std::move(table)) {}

std::string_view Language::get(std::string_view key) const noexcept {
    auto it = _table.find(key);
    if (it != _table.end())
        return it->second;
    return key;
}

std::vector<std::pair<std::string_view, std::string_view>>
Language::get_section(std::string_view prefix) const {
    std::vector<std::pair<std::string_view, std::string_view>> result;
    for (const auto &[key, val] : _table) {
        if (key.substr(0, prefix.size()) == prefix &&
            (key.size() == prefix.size() || key[prefix.size()] == '.')) {
            result.emplace_back(key, val);
        }
    }
    return result;
}

void Language::merge(const Language& other) {
    for (const auto& [k, v] : other._table) {
        _table[k] = v;
    }
}

std::string Language::substitute_impl(std::string_view pattern, const std::vector<std::string> &args) {
    std::string result;
    result.reserve(pattern.size());

    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size()) {
            size_t end = pattern.find('}', i);
            if (end != std::string::npos) {
                std::string_view num(pattern.data() + i + 1, end - i - 1);
                int idx        = 0;
                auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), idx);
                if (ec == std::errc{} && ptr == num.data() + num.size() && idx >= 0 &&
                    static_cast<size_t>(idx) < args.size()) {
                    result += args[static_cast<size_t>(idx)];
                    i = end;
                    continue;
                }
            }
        }
        result += pattern[i];
    }
    return result;
}

// ---- LanguageManager -------------------------------------------------

LanguageManager &LanguageManager::instance() {
    static LanguageManager mgr;
    return mgr;
}

void LanguageManager::register_language(Language lang) {
    auto [it, _] = _langs.emplace(lang.name(), std::move(lang));
    if (!_active)
        _active = &it->second;
}

bool LanguageManager::select(std::string_view code) {
    // 1. Exact match
    auto it = _langs.find(code);
    if (it != _langs.end()) {
        _active = &it->second;
        return true;
    }
    
    // 2. Fallback to en_US
    auto fb = _langs.find(kDefaultLang);
    if (fb != _langs.end()) {
        _active = &fb->second;
        return false;
    }
    return false;
}

const Language &LanguageManager::active() const noexcept {
    static const Language fallback("", {});
    return _active ? *_active : fallback;
}

std::vector<std::string> LanguageManager::available() const {
    std::vector<std::string> codes{_langs.size()};
    for (const auto &[code, _] : _langs)
        codes.push_back(code);
    return codes;
}

std::string LanguageManager::resolve_locale(std::string_view locale) const {
    // 1. Exact match
    if (_langs.contains(locale))
        return std::string(locale);

    // 2. Normalized match
    {
        std::string norm = normalize(locale);
        for (const auto &[code, _] : _langs) {
            if (normalize(code) == norm)
                return code;
        }
    }

    // 3. Language-only match (on normalized)
    {
        std::string norm = normalize(locale);
        auto underscore  = norm.find('_');
        if (underscore != std::string::npos) {
            std::string lang_prefix = norm.substr(0, underscore);
            for (const auto &[code, _] : _langs) {
                std::string cnorm = normalize(code);
                if (cnorm.substr(0, lang_prefix.size()) == lang_prefix &&
                    (cnorm.size() == lang_prefix.size() || cnorm[lang_prefix.size()] == '_')) {
                    return code;
                }
            }
        }
    }

    // 4. Fallback
    return std::string(kDefaultLang);
}
