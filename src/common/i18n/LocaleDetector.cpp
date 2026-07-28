#include "LocaleDetector.h"
#ifndef _WIN32
#include "common/utils/EnvUtil.hpp"
#endif
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
    {
        std::string lang = get_env_str("LANG");
        if (!lang.empty()) {
            auto dot = lang.find('.');
            if (dot != std::string::npos) lang = lang.substr(0, dot);
            return lang;
        }
    }
#else
    // Linux: LC_ALL > LC_MESSAGES > LANG
    {
        std::string lang = get_env_str("LC_ALL");
        if (lang.empty()) lang = get_env_str("LC_MESSAGES");
        if (lang.empty()) lang = get_env_str("LANG");
        if (!lang.empty()) {
            auto dot = lang.find('.');
            if (dot != std::string::npos) lang = lang.substr(0, dot);
            return lang;
        }
    }
#endif
    return std::string(kDefaultLocale);
}
