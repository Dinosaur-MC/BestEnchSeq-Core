#pragma once
#include "utils/BoundedMPMCQueue.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warn,
    Error
};

struct LogEntry {
    LogLevel level;
    std::string message;
};

/// Async logger: messages pushed to a lock-free MPMC queue from any thread.
/// Dedicated worker writes to logs/<timestamp>.log + latest.log.
/// Rotation keeps at most 5 historic runs.
class Logger {
public:
    explicit Logger(std::string log_dir = "logs");
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Push a log message (non-blocking). Silently drops when queue full.
    void log(LogLevel level, std::string message);

    /// Convenience helpers.
    void info(std::string msg)  { log(LogLevel::Info,  std::move(msg)); }
    void warn(std::string msg)  { log(LogLevel::Warn,  std::move(msg)); }
    void error(std::string msg) { log(LogLevel::Error, std::move(msg)); }
    void debug(std::string msg) { log(LogLevel::Debug, std::move(msg)); }

    /// printf-style format and push.
    template<typename... Args>
    void printf(LogLevel level, const char* fmt, Args&&... args) {
        int sz = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (sz < 0) return;
        std::string buf(static_cast<size_t>(sz) + 1, '\0');
        std::snprintf(buf.data(), buf.size(), fmt, std::forward<Args>(args)...);
        buf.resize(static_cast<size_t>(sz));
        log(level, std::move(buf));
    }

    /// Flush pending messages synchronously.
    void flush();

private:
    void _worker();
    void _rotate();

    BoundedMPMCQueue<LogEntry, 256> _queue;
    std::atomic<bool> _running{true};
    std::thread _worker_thread;
    std::string _log_dir;
};
