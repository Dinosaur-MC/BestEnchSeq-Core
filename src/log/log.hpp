#pragma once

/// @file log/log.hpp
/// Convenience free-function wrappers for the global Logger singleton.
/// Include this instead of log/Logger.hpp for everyday use.
///
/// Usage:
///   besq::log::info("hello world");
///   besq::log::warn("something suspicious");
///   besq::log::error("something broke");
///   besq::log::printf(LogLevel::Info, "value = %d", 42);

#include "log/Logger.hpp"

namespace besq {
namespace log {

inline void info(std::string msg)    { Logger::instance().info(std::move(msg)); }
inline void warn(std::string msg)    { Logger::instance().warn(std::move(msg)); }
inline void error(std::string msg)   { Logger::instance().error(std::move(msg)); }
inline void debug(std::string msg)   { Logger::instance().debug(std::move(msg)); }

template <typename... Args>
inline void printf(LogLevel level, const char* fmt, Args&&... args) {
    Logger::instance().printf(level, fmt, std::forward<Args>(args)...);
}

} // namespace log
} // namespace besq
