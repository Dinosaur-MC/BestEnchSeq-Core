#include "LocaleDetector.h"
#include <cstdlib>
#include <string>
#include <string_view>
#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static constexpr std::string_view kDefaultLocale = "en_US";

std::string detect_system_locale() {
#if defined(_WIN32)
    wchar_t buf[LOCALE_NAME_MAX_LENGTH];
    int n = GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
    if (n > 0) {
        std::wstring ws(buf);
        std::string result(ws.begin(), ws.end());
        std::replace(result.begin(), result.end(), '-', '_');
        return result;
    }
#elif defined(__APPLE__)
    const char* lang = std::getenv("LANG");
    if (lang && *lang) {
        std::string s(lang);
        auto dot = s.find('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    }
#else
    // Linux: LC_ALL > LC_MESSAGES > LANG
    const char* lang = std::getenv("LC_ALL");
    if (!lang || !*lang) lang = std::getenv("LC_MESSAGES");
    if (!lang || !*lang) lang = std::getenv("LANG");
    if (lang && *lang) {
        std::string s(lang);
        auto dot = s.find('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    }
#endif
    return std::string(kDefaultLocale);
}
