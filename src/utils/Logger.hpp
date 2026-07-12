#pragma once
#include "utils/queue/BoundedMPMCQueue.hpp"
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
///
/// Zero-CPU idle: when no messages are queued the worker thread blocks on
/// std::atomic::wait (C++20) — no mutex, no condition variable, no polling.
///
/// Singleton (Meyer's): call Logger::instance() from anywhere.
/// Constructed on first use; log dir defaults to "logs".
class Logger {
public:
    static Logger& instance();

    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Push a log message (non-blocking). Silently drops when queue full.
    /// Wakes the worker thread via atomic notify_one.
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
    explicit Logger(std::string log_dir = "logs");

    void _worker();
    void _rotate();

    BoundedMPMCQueue<LogEntry, 256> _queue;
    std::atomic<bool> _running{true};
    /// Wake sequence counter: every push() increments this and notifies the
    /// worker via C++20 atomic::notify_one.  The worker blocks on wait()
    /// instead of polling — zero CPU when idle, no mutex or CV involved.
    std::atomic<uint64_t> _wake_seq{0};
    std::thread _worker_thread;
    std::string _log_dir;
};
