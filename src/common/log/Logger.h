#pragma once
#include "LogTypes.h"
#include "utils/EventLoop.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/// Async logger: messages pushed to a lock-free MPMC queue from any thread.
/// Dedicated worker consumes entries and runs them through the consumer chain
/// (console mirror → file sink → test capture → future persistence).
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

    /// SYNC path: print to the console mirror IMMEDIATELY (gated by
    /// console_enabled/console_level), then enqueue for the async file sink
    /// with the console_printed flag so the worker's ConsoleConsumer skips it.
    /// Use for low-frequency, user-visible diagnostics (startup, errors);
    /// hot paths should use the ASYNC helpers (log()/info_fmt_async) instead.
    void log_sync(LogLevel level, std::string message);

    /// Convenience helpers (plain string) — ASYNC (queue → consumer chain).
    void info(std::string msg) { log(LogLevel::Info, std::move(msg)); }
    void warn(std::string msg) { log(LogLevel::Warn, std::move(msg)); }
    void error(std::string msg) { log(LogLevel::Error, std::move(msg)); }
    void debug(std::string msg) { log(LogLevel::Debug, std::move(msg)); }

    /// Convenience helpers (plain string) — SYNC console + async file.
    void info_sync(std::string msg) { log_sync(LogLevel::Info, std::move(msg)); }
    void warn_sync(std::string msg) { log_sync(LogLevel::Warn, std::move(msg)); }
    void error_sync(std::string msg) { log_sync(LogLevel::Error, std::move(msg)); }
    void debug_sync(std::string msg) { log_sync(LogLevel::Debug, std::move(msg)); }

    /// printf-style format helpers — ASYNC.
    template <typename... Args> void info_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void warn_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void error_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void debug_fmt(const char* fmt, Args&&... args) {
        printf(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    /// printf-style format helpers — SYNC console + async file.
    template <typename... Args> void info_fmt_sync(const char* fmt, Args&&... args) {
        printf_sync(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void warn_fmt_sync(const char* fmt, Args&&... args) {
        printf_sync(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void error_fmt_sync(const char* fmt, Args&&... args) {
        printf_sync(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void debug_fmt_sync(const char* fmt, Args&&... args) {
        printf_sync(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    /// printf-style format and push — ASYNC.
    template <typename... Args> void printf(LogLevel level, const char* fmt, Args&&... args) {
        _format_and_log(level, fmt, std::forward<Args>(args)...);
    }

    /// printf-style format, SYNC console + async file.
    template <typename... Args> void printf_sync(LogLevel level, const char* fmt, Args&&... args) {
        _format_and_log_sync(level, fmt, std::forward<Args>(args)...);
    }

    /// Flush pending messages synchronously.
    void flush();

    /// ── Consumer chain ──────────────────────────────────────────────────
    /// Everything that reads logs is a consumer (console, file, test capture,
    /// future persistence).  Registered consumers are invoked serially on the
    /// worker thread, in registration order — a read-only "middleware" chain.
    ///
    /// add_consumer takes effect immediately (even before the worker starts);
    /// returns a token for remove_consumer.  A consumer that throws is caught
    /// and dropped — one broken consumer does not break the chain.
    using ConsumerId = uint64_t;
    ConsumerId add_consumer(std::function<void(const LogEntry&)> consumer);
    void remove_consumer(ConsumerId id);

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

private:
    using ConsumerFn = std::function<void(const LogEntry&)>;

    /// Format into a string (shared by the async printf and the sync
    /// printf_sync; avoids duplicating the snprintf dance).
    template <typename... Args> static std::string _format(const char* fmt, Args&&... args) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
        int sz = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (sz < 0)
            return {};
        std::string buf(static_cast<size_t>(sz) + 1, '\0');
        std::snprintf(buf.data(), buf.size(), fmt, std::forward<Args>(args)...);
#pragma clang diagnostic pop
        buf.resize(static_cast<size_t>(sz));
        return buf;
    }
    template <typename... Args> void _format_and_log(LogLevel level, const char* fmt, Args&&... args) {
        log(level, _format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args> void _format_and_log_sync(LogLevel level, const char* fmt, Args&&... args) {
        log_sync(level, _format(fmt, std::forward<Args>(args)...));
    }

    // ── Consumer chain ──────────────────────────────────────────────────
    // The consumer table.  Guarded by a small mutex: registration/removal
    // happens at startup or in tests, the chain traversal runs on the worker.
    struct ConsumerChain {
        std::mutex mutex;
        std::vector<std::pair<ConsumerId, ConsumerFn>> consumers;
        ConsumerId next_id = 0;
    };

    /// EventLoop data-mode handler: forwards each dequeued entry to the
    /// consumer chain — snapshot the table under the lock, then invoke the
    /// consumers outside the lock, each in its own try/catch (a broken
    /// consumer is dropped, the chain continues); the processed counter is
    /// bumped at the end of the chain.
    struct ChainHandler {
        ConsumerChain* chain;
        std::atomic<uint64_t>* processed_ptr;
        void operator()(LogEntry entry);
    };

    // ── Console consumer ───────────────────────────────────────────────
    // Mirror log lines to the terminal: Warn/Error → stderr, Debug/Info →
    // stdout.  Gated by the console threshold (_console_level) and the
    // _console_enabled flag — semantics unchanged from the old FileHandler.
    struct ConsoleConsumer {
        explicit ConsoleConsumer(std::atomic<bool>* ce, std::atomic<LogLevel>* cl)
            : console_enabled_ptr(ce), console_level_ptr(cl) {}
        void operator()(const LogEntry& entry) const;

        std::atomic<bool>* console_enabled_ptr;
        std::atomic<LogLevel>* console_level_ptr;
    };

    // ── File consumer ──────────────────────────────────────────────────
    // Write entries gated by the file threshold (_level) to
    // logs/<timestamp>.log + latest.log, with rotation keeping at most
    // `retention` historic runs.  Files are opened in the constructor
    // (before the worker starts) and written from the worker thread.
    struct FileConsumer {
        explicit FileConsumer(std::string log_dir, size_t* rp, std::atomic<LogLevel>* fl);
        void operator()(const LogEntry& entry);
        void rotate();

        std::string log_dir;
        std::ofstream run_file;
        std::ofstream latest_file;
        size_t* retention_ptr;
        std::atomic<LogLevel>* file_level_ptr;
    };

    explicit Logger(const LoggerConfig& cfg = {});

    // _console_*, _max_retention, _processed and _chain MUST precede _loop:
    // their addresses are captured by _loop's ChainHandler initializer.
    // C++ initializes members in declaration order, so these must come
    // before _loop.
    std::atomic<uint64_t> _enqueued{0};
    std::atomic<uint64_t> _processed{0};
    size_t _max_retention{5};
    std::atomic<bool> _console_enabled{true};
    std::atomic<LogLevel> _console_level{LogLevel::Warn};
    std::atomic<LogLevel> _level{LogLevel::Debug};
    /// Console mirror instance used BOTH by the async worker chain (registered
    /// as a consumer in the ctor) and by the sync log_sync() path.  fprintf on
    /// a FILE* is thread-safe, so the sync path needs no extra lock.
    ConsoleConsumer _console{&_console_enabled, &_console_level};
    ConsumerChain _chain;
    EventLoop<LogEntry, SegmentedMPSCQueue<LogEntry>, ChainHandler> _loop{ChainHandler{&_chain, &_processed}};
};
