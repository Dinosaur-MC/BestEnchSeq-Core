#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>

namespace {
inline const char *getenv_safe(const char *name) noexcept {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    return std::getenv(name);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}
} // namespace

/// Set an environment variable (cross-platform).
///
/// On Windows uses ``_putenv_s``; on POSIX uses ``setenv``.
/// Passing ``value = ""`` **unsets** the variable (calls ``unsetenv`` on POSIX,
/// sets to empty string on Windows).
inline void set_env(const char *name, const char *value) noexcept {
    if (!name || !name[0])
        return;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value && value[0])
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

/// Unset an environment variable (cross-platform).
inline void unset_env(const char *name) noexcept {
    if (!name || !name[0])
        return;
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/// Read an environment variable as a string.
///
/// Returns \p default_val when the variable is not set or the name is
/// null/empty.  An empty-string variable is treated as set (returns "").
inline std::string get_env_str(const char *name, const std::string &default_val = "") noexcept {
    if (!name || !name[0])
        return default_val;
    const char *raw = getenv_safe(name);
    if (!raw)
        return default_val;
    return std::string(raw); // construct immediately; raw pointer is ephemeral
}

/// Read an environment variable and convert to type \p T.
///
/// Supported types:
///   - ``std::string``       — raw value
///   - ``bool``              — ``"true"``/``"1"`` → true, ``"false"``/``"0"`` → false
///   - Arithmetic types      — parsed via ``std::from_chars``
///   - Any other type        — always returns \p default_val
///
/// Returns \p default_val when the variable is unset, empty, or
/// conversion fails.
///
/// Example (built-in conversions):
/// @code
///   auto mb   = get_env<int64_t>("BESQ_MEMORY_MB", 2048);
///   auto name = get_env<std::string>("BESQ_PROFILE", "default");
///   auto flag = get_env<bool>("BESQ_VERBOSE", false);
/// @endcode
///
/// When a \p convert callable is provided, it receives the string value
/// and must return \p T.  Exceptions thrown by the converter are caught
/// and cause \p default_val to be returned.
///
/// Example (custom converter):
/// @code
///   auto data = get_env<MyType>("VAR", MyType{},
///                   [](std::string_view sv) { return MyType::parse(sv); });
/// @endcode
///   auto data = util::get_env<MyType>("VAR", MyType{},
///                       [](std::string_view sv) { return MyType::parse(sv); });
/// @endcode
template <typename T> T get_env(const char *name, T default_val) noexcept {
    auto raw = get_env_str(name, std::string{});
    if (raw.empty())
        return default_val;

    if constexpr (std::is_same_v<T, std::string>) {
        return raw;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (raw == "true" || raw == "1")
            return true;
        if (raw == "false" || raw == "0")
            return false;
        return default_val;
    } else if constexpr (std::is_arithmetic_v<T>) {
        T val{};
        auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
        if (ec == std::errc{} && ptr == raw.data() + raw.size())
            return val;
        return default_val;
    } else {
        return default_val;
    }
}

/// Overload with a custom converter.
///
/// The converter receives ``std::string_view`` and must return \p T.
/// Exceptions are caught and cause \p default_val to be returned.
template <typename T, typename Converter> T get_env(const char *name, T default_val, Converter &&convert) noexcept {
    auto raw = get_env_str(name, std::string{});
    if (raw.empty())
        return default_val;
    try {
        return std::forward<Converter>(convert)(std::string_view{raw});
    } catch (...) {
        return default_val;
    }
}
