#pragma once

/// @file log/log.hpp
/// Convenience wrappers and source-location macros for the global Logger.
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

#include "log/Logger.hpp"

namespace besq {
namespace log {

// ─── Plain-string helpers ───────────────────────────────────────────

inline void info(std::string msg)  { Logger::instance().info(std::move(msg)); }
inline void warn(std::string msg)  { Logger::instance().warn(std::move(msg)); }
inline void error(std::string msg) { Logger::instance().error(std::move(msg)); }
inline void debug(std::string msg) { Logger::instance().debug(std::move(msg)); }

// ─── printf-style helpers (no file:line capture) ────────────────────

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

// ─── Macros (capture __FILE__ / __LINE__ automatically) ──────────────
// String-literal concatenation embeds the prefix at compile time:
//   "[file:line] fmt" → one string, one snprintf call.

// Two-level stringify so __LINE__ expands before being stringified.
// (A single-level #x would stringify the token "__LINE__" literally.)
#define BESQ_LOG_STR2(x)  #x
#define BESQ_LOG_STR(x)   BESQ_LOG_STR2(x)

// With -fmacro-prefix-map=src/=, __FILE__ gives short paths like
// "parsers/InputParser.cpp" instead of absolute paths.
#define BESQ_LOG_LOC     "[" __FILE__ ":" BESQ_LOG_STR(__LINE__) "] "

/// @brief  Log at Info level with source-location prefix.
///         Accepts printf-style format + args.
#define LOG_INFO(fmt, ...)   ::besq::log::info_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   ::besq::log::warn_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  ::besq::log::error_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  ::besq::log::debug_fmt(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
