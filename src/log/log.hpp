#pragma once

/// @file log/log.hpp
/// Convenience wrappers and source-location macros for the global Logger.
///
/// When BESQ_DISABLE_LOGGER is defined, all besq::log:: functions become
/// no-op stubs — no Logger singleton, no thread, no file I/O.  This lets
/// minimal builds use code that would otherwise pull in the Logger.
///
/// Plain string:
///   besq::log::info("hello");
///   LOG_INFO("hello");                     // adds [file:line] prefix
///
/// printf-style:
///   besq::log::info_fmt("value = %d", 42);
///   LOG_INFO("value = %d", 42);            // adds [file:line] prefix
///
/// Generic (must specify level):
///   besq::log::printf(LogLevel::Warn, "x = %d", x);

#include "types/LogTypes.h"
#include <string>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  When logging is disabled all besq::log:: functions are empty stubs
// ─────────────────────────────────────────────────────────────────────────────

#ifdef BESQ_DISABLE_LOGGER

namespace besq {
namespace log {

// ─── Plain-string stubs ─────────────────────────────────────────────────
inline void info(std::string)  {}
inline void warn(std::string)  {}
inline void error(std::string) {}
inline void debug(std::string) {}

// ─── printf-style stubs ─────────────────────────────────────────────────
template <typename... Args>
inline void info_fmt(const char*, Args&&...) {}
template <typename... Args>
inline void warn_fmt(const char*, Args&&...) {}
template <typename... Args>
inline void error_fmt(const char*, Args&&...) {}
template <typename... Args>
inline void debug_fmt(const char*, Args&&...) {}

template <typename... Args>
inline void printf(LogLevel, const char*, Args&&...) {}

} // namespace log
} // namespace besq

#else // !BESQ_DISABLE_LOGGER

#include "log/Logger.h"

namespace besq {
namespace log {

// ─── Plain-string helpers ───────────────────────────────────────────────

inline void info(std::string msg)  { Logger::instance().info(std::move(msg)); }
inline void warn(std::string msg)  { Logger::instance().warn(std::move(msg)); }
inline void error(std::string msg) { Logger::instance().error(std::move(msg)); }
inline void debug(std::string msg) { Logger::instance().debug(std::move(msg)); }

// ─── printf-style helpers (no file:line capture) ────────────────────────

template <typename... Args>
inline void info_fmt(const char* fmt, Args&&... args) {
    Logger::instance().info_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void warn_fmt(const char* fmt, Args&&... args) {
    Logger::instance().warn_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void error_fmt(const char* fmt, Args&&... args) {
    Logger::instance().error_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void debug_fmt(const char* fmt, Args&&... args) {
    Logger::instance().debug_fmt(fmt, std::forward<Args>(args)...);
}

/// Generic printf (explicit level).
template <typename... Args>
inline void printf(LogLevel level, const char* fmt, Args&&... args) {
    Logger::instance().printf(level, fmt, std::forward<Args>(args)...);
}

} // namespace log
} // namespace besq

#endif // BESQ_DISABLE_LOGGER

// ─── Macros (capture __FILE__ / __LINE__ automatically) ─────────────────
// These work identically regardless of the BESQ_DISABLE_LOGGER setting;
// when logging is disabled the template helpers are no-ops, so the
// argument expressions are still type-checked but produce no code.
//
// String-literal concatenation embeds the prefix at compile time:
//   "[file:line] fmt" → one string, one snprintf call.

// Two-level stringify so __LINE__ expands before being stringified.
// (A single-level #x would stringify the token "__LINE__" literally.)
#define BESQ_LOG_STR2(x)  #x
#define BESQ_LOG_STR(x)   BESQ_LOG_STR2(x)

// With -fmacro-prefix-map=src/=, __FILE__ gives short paths like
// "parsers/EnchParser.cpp" instead of absolute paths.
#define BESQ_LOG_LOC     "[" __FILE__ ":" BESQ_LOG_STR(__LINE__) "] "

/// @brief  Log at Info level with source-location prefix.
///         Accepts printf-style format + args.
/// MSVC's traditional preprocessor does not support __VA_OPT__ (C++20),
/// so we fall back to the ##__VA_ARGS__ GNU extension for MSVC.
#if defined(_MSC_VER) && !defined(__clang__)
#  define LOG_INFO(fmt, ...)   ::besq::log::info_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_WARN(fmt, ...)   ::besq::log::warn_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_ERROR(fmt, ...)  ::besq::log::error_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_DEBUG(fmt, ...)  ::besq::log::debug_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#else
#  define LOG_INFO(fmt, ...)   ::besq::log::info_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_WARN(fmt, ...)   ::besq::log::warn_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_ERROR(fmt, ...)  ::besq::log::error_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_DEBUG(fmt, ...)  ::besq::log::debug_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#endif
