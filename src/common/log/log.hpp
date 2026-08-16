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

#include "LogTypes.h"
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
inline void info_async(std::string)  {}
inline void warn_async(std::string)  {}
inline void error_async(std::string) {}
inline void debug_async(std::string) {}
inline void flush()            {}

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
inline void info_fmt_async(const char*, Args&&...) {}
template <typename... Args>
inline void warn_fmt_async(const char*, Args&&...) {}
template <typename... Args>
inline void error_fmt_async(const char*, Args&&...) {}
template <typename... Args>
inline void debug_fmt_async(const char*, Args&&...) {}

template <typename... Args>
inline void printf(LogLevel, const char*, Args&&...) {}
template <typename... Args>
inline void printf_async(LogLevel, const char*, Args&&...) {}

} // namespace log
} // namespace besq

#else // !BESQ_DISABLE_LOGGER

#include "Logger.h"

// ─── Logger setup helper (avoids direct Logger::instance() calls) ─────────

/// Apply a LoggerConfig to the global Logger singleton.  Defaults match the
/// AppConfig defaults (Debug, retention 5, console on at Warn).
inline void setup_logger(const LoggerConfig &cfg = {}) {
    Logger::instance().set_level(
        cfg.level >= 3 ? LogLevel::Error
      : cfg.level >= 2 ? LogLevel::Warn
      : cfg.level >= 1 ? LogLevel::Info
      :                  LogLevel::Debug);
    Logger::instance().set_retention(cfg.retention);
    Logger::instance().set_console_enabled(cfg.console_enabled);
    Logger::instance().set_console_level(
        cfg.console_level >= 3 ? LogLevel::Error
      : cfg.console_level >= 2 ? LogLevel::Warn
      : cfg.console_level >= 1 ? LogLevel::Info
      :                          LogLevel::Debug);
}

namespace besq {
namespace log {

// ─── Plain-string helpers ───────────────────────────────────────────────
// SYNC (console printed immediately, file via the async queue).  Use the
// _async variants for hot paths.

inline void info(std::string msg)  { Logger::instance().info_sync(std::move(msg)); }
inline void warn(std::string msg)  { Logger::instance().warn_sync(std::move(msg)); }
inline void error(std::string msg) { Logger::instance().error_sync(std::move(msg)); }
inline void debug(std::string msg) { Logger::instance().debug_sync(std::move(msg)); }

// ─── Plain-string helpers — ASYNC (queue → consumer chain) ─────────────

inline void info_async(std::string msg)  { Logger::instance().info(std::move(msg)); }
inline void warn_async(std::string msg)  { Logger::instance().warn(std::move(msg)); }
inline void error_async(std::string msg) { Logger::instance().error(std::move(msg)); }
inline void debug_async(std::string msg) { Logger::instance().debug(std::move(msg)); }

// ─── printf-style helpers (no file:line capture) — SYNC ────────────────

template <typename... Args>
inline void info_fmt(const char* fmt, Args&&... args) {
    Logger::instance().info_fmt_sync(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void warn_fmt(const char* fmt, Args&&... args) {
    Logger::instance().warn_fmt_sync(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void error_fmt(const char* fmt, Args&&... args) {
    Logger::instance().error_fmt_sync(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void debug_fmt(const char* fmt, Args&&... args) {
    Logger::instance().debug_fmt_sync(fmt, std::forward<Args>(args)...);
}

// ─── printf-style helpers (no file:line capture) — ASYNC ───────────────

template <typename... Args>
inline void info_fmt_async(const char* fmt, Args&&... args) {
    Logger::instance().info_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void warn_fmt_async(const char* fmt, Args&&... args) {
    Logger::instance().warn_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void error_fmt_async(const char* fmt, Args&&... args) {
    Logger::instance().error_fmt(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void debug_fmt_async(const char* fmt, Args&&... args) {
    Logger::instance().debug_fmt(fmt, std::forward<Args>(args)...);
}

/// Generic printf (explicit level) — SYNC.
template <typename... Args>
inline void printf(LogLevel level, const char* fmt, Args&&... args) {
    Logger::instance().printf_sync(level, fmt, std::forward<Args>(args)...);
}

/// Generic printf (explicit level) — ASYNC.
template <typename... Args>
inline void printf_async(LogLevel level, const char* fmt, Args&&... args) {
    Logger::instance().printf(level, fmt, std::forward<Args>(args)...);
}

/// Block until the async Logger's queue is drained.  The CLI calls this before
/// exit — the worker thread's queued WARN/ERROR console lines can otherwise be
/// lost when the process exits right after the last log() (e.g. the plugin
/// audit's "[Audit] REFUSED" on --list-algorithms; the DLL's static Logger
/// destructor does not reliably drain before the runtime closes stderr).
inline void flush() { Logger::instance().flush(); }

} // namespace log
} // namespace besq

#endif // BESQ_DISABLE_LOGGER

// ─── Macros (capture __FILE__ / __LINE__ automatically) ─────────────────
// These work identically regardless of the BESQ_DISABLE_LOGGER setting;
// when logging is disabled the template helpers are no-ops, so the
// argument expressions are still type-checked but produce no code.
//
// LOG_*        → SYNC console (printed immediately) + async file sink.
//                Use for low-frequency, user-visible diagnostics.
// LOG_*_ASYNC  → fully async (queue → consumer chain).  Use on HOT PATHS
//                (algorithm domain): no blocking, no fflush on the caller.
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
#  define LOG_INFO_ASYNC(fmt, ...)   ::besq::log::info_fmt_async(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_WARN_ASYNC(fmt, ...)   ::besq::log::warn_fmt_async(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_ERROR_ASYNC(fmt, ...)  ::besq::log::error_fmt_async(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#  define LOG_DEBUG_ASYNC(fmt, ...)  ::besq::log::debug_fmt_async(BESQ_LOG_LOC fmt, ##__VA_ARGS__)
#else
#  define LOG_INFO(fmt, ...)   ::besq::log::info_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_WARN(fmt, ...)   ::besq::log::warn_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_ERROR(fmt, ...)  ::besq::log::error_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_DEBUG(fmt, ...)  ::besq::log::debug_fmt(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_INFO_ASYNC(fmt, ...)   ::besq::log::info_fmt_async(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_WARN_ASYNC(fmt, ...)   ::besq::log::warn_fmt_async(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_ERROR_ASYNC(fmt, ...)  ::besq::log::error_fmt_async(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#  define LOG_DEBUG_ASYNC(fmt, ...)  ::besq::log::debug_fmt_async(BESQ_LOG_LOC fmt __VA_OPT__(,) __VA_ARGS__)
#endif
