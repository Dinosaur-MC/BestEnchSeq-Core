#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <limits>

namespace util {

/// Read an environment variable as a signed 64-bit integer.
///
/// Returns \p default_val when:
///   - The variable is not set (or set to an empty string).
///   - The value is not a valid integer (non-digit characters, empty).
///   - The value overflows int64_t.
///
/// Leading/trailing whitespace is NOT accepted (rejected as invalid)
/// to avoid ambiguity with empty/unset variables.
///
/// Example:
/// @code
///   int64_t mb = util::get_env_int("BESQ_MEMORY_MB", 2048);
/// @endcode
inline int64_t get_env_int(const char* name, int64_t default_val) noexcept {
    if (!name || !name[0]) return default_val;

    const char* raw = std::getenv(name);
    if (!raw || !raw[0]) return default_val;

    // Parse with full validation
    const char* p = raw;

    // Optional sign
    bool negative = false;
    if (*p == '-') { negative = true; ++p; }
    else if (*p == '+') { ++p; }

    // Must have at least one digit
    if (!*p || !std::isdigit(static_cast<unsigned char>(*p)))
        return default_val;

    // Accumulate with overflow check
    int64_t val = 0;
    constexpr int64_t MAX_PRE_DIV = std::numeric_limits<int64_t>::max() / 10;
    while (*p && std::isdigit(static_cast<unsigned char>(*p))) {
        int d = *p - '0';
        if (val > MAX_PRE_DIV)
            return default_val;          // would overflow on next multiply
        val *= 10;
        if (val > std::numeric_limits<int64_t>::max() - d)
            return default_val;          // would overflow on addition
        val += d;
        ++p;
    }

    // Reject trailing non-digit characters (e.g. "123abc")
    if (*p) return default_val;

    if (negative)
        val = -val;

    // Reject zero / negative for physical quantities
    if (val <= 0) return default_val;

    return val;
}

} // namespace util
