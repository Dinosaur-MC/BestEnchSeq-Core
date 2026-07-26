#include "Language.h"
#include <charconv>
#include <cstdlib>

namespace {
    constexpr std::string_view kDefaultLang = "en_US";
}

Language::Language(std::string name, Table table)
    : _name(std::move(name)), _table(std::move(table)) {}

std::string_view Language::get(std::string_view key) const noexcept {
    auto it = _table.find(std::string(key));
    if (it != _table.end())
        return it->second;
    return key;
}

std::vector<std::pair<std::string_view, std::string_view>>
Language::get_section(std::string_view prefix) const {
    std::vector<std::pair<std::string_view, std::string_view>> result;
    for (const auto& [key, val] : _table) {
        if (key.substr(0, prefix.size()) == prefix &&
            (key.size() == prefix.size() || key[prefix.size()] == '.')) {
            result.emplace_back(key, val);
        }
    }
    return result;
}

std::string Language::substitute_impl(
    std::string_view pattern,
    const std::vector<std::string>& args) {
    std::string result;
    result.reserve(pattern.size());

    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size()) {
            size_t end = pattern.find('}', i);
            if (end != std::string::npos) {
                std::string_view num(pattern.data() + i + 1, end - i - 1);
                int idx          = 0;
                auto [ptr, ec]   = std::from_chars(num.data(), num.data() + num.size(), idx);
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

LanguageManager& LanguageManager::instance() {
    static LanguageManager mgr;
    return mgr;
}

void LanguageManager::register_language(Language lang) {
    auto [it, _] = _langs.emplace(std::string(lang.name()), std::move(lang));
    if (!_active)
        _active = &it->second;
}

bool LanguageManager::select(std::string_view code) {
    auto it = _langs.find(std::string(code));
    if (it != _langs.end()) {
        _active = &it->second;
        return true;
    }
    // Fallback to en_US
    auto fb = _langs.find(std::string(kDefaultLang));
    if (fb != _langs.end()) {
        _active = &fb->second;
        return false;
    }
    return false;
}

const Language& LanguageManager::active() const noexcept {
    static const Language fallback("", {});
    return _active ? *_active : fallback;
}

std::vector<std::string> LanguageManager::available() const {
    std::vector<std::string> codes;
    codes.reserve(_langs.size());
    for (const auto& [code, _] : _langs)
        codes.push_back(code);
    return codes;
}

std::string LanguageManager::resolve_locale(std::string_view locale) const {
    // 1. Exact match
    if (_langs.count(std::string(locale)))
        return std::string(locale);

    // 2. Language-only match
    auto underscore = locale.find('_');
    if (underscore != std::string::npos) {
        std::string lang(locale.substr(0, underscore));
        for (const auto& [code, _] : _langs) {
            if (code.compare(0, lang.size(), lang) == 0 &&
                (code.size() == lang.size() || code[lang.size()] == '_')) {
                return code;
            }
        }
    }

    // 3. Fallback
    return std::string(kDefaultLang);
}
