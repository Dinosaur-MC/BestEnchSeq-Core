#pragma once
#include "utils/queue/BoundedMPMCQueue.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
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
/// Poll-free idle: when no messages are queued the worker thread blocks on a
/// condition variable (zero CPU, zero scheduler noise) instead of busy-waiting
/// or short-sleep polling.  This eliminates ~200 context-switches/second that
/// previously caused variable cache interference with computation threads.
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
    /// Wakes the worker thread via condition variable.
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
    std::mutex _wake_mtx;
    std::condition_variable _wake_cv;
    std::thread _worker_thread;
    std::string _log_dir;
};
