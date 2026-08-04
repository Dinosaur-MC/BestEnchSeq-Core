#pragma once
#include "LogTypes.h"
#include "utils/EventLoop.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

class LogRingBuffer;  // common/log/LogRingBuffer.h — forward decl only here

/// Async logger: messages pushed to a lock-free MPMC queue from any thread.
/// Dedicated worker writes to logs/<timestamp>.log + latest.log.
/// Rotation keeps at most 5 historic runs.
///
/// Zero-CPU idle: when no messages are queued the worker thread blocks in
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
    void log(LogLevel level, std::string message);

    /// Convenience helpers (plain string).
    void info(std::string msg)  { log(LogLevel::Info,  std::move(msg)); }
    void warn(std::string msg)  { log(LogLevel::Warn,  std::move(msg)); }
    void error(std::string msg) { log(LogLevel::Error, std::move(msg)); }
    void debug(std::string msg) { log(LogLevel::Debug, std::move(msg)); }

    /// printf-style format helpers.
    template<typename... Args>
    void info_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void warn_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void error_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void debug_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    /// printf-style format and push.
    template<typename... Args>
    void printf(LogLevel level, const char* fmt, Args&&... args) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
        int sz = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (sz < 0) return;
        std::string buf(static_cast<size_t>(sz) + 1, '\0');
        std::snprintf(buf.data(), buf.size(), fmt, std::forward<Args>(args)...);
#pragma clang diagnostic pop
        buf.resize(static_cast<size_t>(sz));
        log(level, std::move(buf));
    }

    /// Flush pending messages synchronously.
    void flush();

    /// ── Runtime configuration ────────────────────────────────────────────

    void set_level(LogLevel lv) noexcept { _level.store(lv, std::memory_order_release); }
    LogLevel get_level() const noexcept { return _level.load(std::memory_order_acquire); }

    void set_retention(size_t n) noexcept { _max_retention = n; }
    size_t get_retention() const noexcept { return _max_retention; }

    /// ── Console output (stdout/stderr mirror) ────────────────────────────
    /// Mirror log lines to the terminal: Warn/Error → stderr, Debug/Info →
    /// stdout.  Enabled by default at Warn level (warnings/errors are the
    /// user-visible diagnostics); Info/Debug stay in files unless the level
    /// is lowered.  The host configures this via AppConfig → setup_logger()
    /// (BESQ_LOG_CONSOLE=0/1, BESQ_LOG_CONSOLE_LEVEL=0..3); override here
    /// programmatically.
    ///
    /// Caveat: lowering the level to Info/Debug routes those lines to STDOUT,
    /// which will interleave with machine-readable CLI output (--format
    /// json/compact also writes stdout) and corrupt it.  Keep the console
    /// level at Warn (default) when using machine output.
    void set_console_enabled(bool on) noexcept { _console_enabled.store(on, std::memory_order_release); }
    bool console_enabled() const noexcept { return _console_enabled.load(std::memory_order_acquire); }
    void set_console_level(LogLevel lv) noexcept { _console_level.store(lv, std::memory_order_release); }
    LogLevel console_level() const noexcept { return _console_level.load(std::memory_order_acquire); }

    /// Optional bounded ring buffer for /api/logs. When set, every log() call
    /// also pushes a copy here (thread-safe, bounded).
    void set_ring_buffer(std::shared_ptr<LogRingBuffer> ring) noexcept {
        _ring_buffer = std::move(ring);
    }
    /// The configured ring buffer (may be null). WebModule reads it for
    /// /api/logs.
    std::shared_ptr<LogRingBuffer> ring_buffer() const noexcept {
        return _ring_buffer;
    }

private:
    // ── FileHandler ────────────────────────────────────────────────────
    // Consumes LogEntry instances on the EventLoop worker thread.
    // Files are opened in the constructor (before worker starts) and
    // written from the worker thread.
    struct FileHandler {
        explicit FileHandler(std::string log_dir,
                             std::atomic<uint64_t>* pp = nullptr,
                             size_t* rp = nullptr,
                             std::atomic<bool>* ce = nullptr,
                             std::atomic<LogLevel>* cl = nullptr,
                             std::atomic<LogLevel>* fl = nullptr);
        void operator()(LogEntry entry);
        void rotate();

        std::string log_dir;
        std::ofstream run_file;
        std::ofstream latest_file;
        std::atomic<uint64_t>* processed_ptr{nullptr};
        size_t* retention_ptr{nullptr};
        std::atomic<bool>* console_enabled_ptr{nullptr};
        std::atomic<LogLevel>* console_level_ptr{nullptr};
        std::atomic<LogLevel>* file_level_ptr{nullptr};
    };

    explicit Logger(const LoggerConfig &cfg = {});

    // _console_*, _max_retention and _processed MUST precede _loop because
    // the FileHandler constructor receives pointers to them (via the _loop
    // initializer list). C++ initializes members in declaration order, so
    // these must come before _loop.
    std::atomic<uint64_t> _enqueued{0};
    std::atomic<uint64_t> _processed{0};
    size_t _max_retention{5};
    std::atomic<bool> _console_enabled{true};
    std::atomic<LogLevel> _console_level{LogLevel::Warn};
    std::atomic<LogLevel> _level{LogLevel::Debug};
    EventLoop<LogEntry, SegmentedMPSCQueue<LogEntry>, FileHandler> _loop;
    std::shared_ptr<LogRingBuffer> _ring_buffer;
};
