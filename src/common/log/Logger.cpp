#include "Logger.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

/// Readable timestamp for log lines: "2026-07-13 08:29:15"
static std::string now_str() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

/// Safe timestamp for filenames: "20260713_082915"
static std::string file_ts() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

static std::string level_str(LogLevel lv) {
    switch (lv) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "????";
}

// ─── FileHandler ──────────────────────────────────────────────────────

Logger::FileHandler::FileHandler(std::string log_dir, std::atomic<uint64_t>* pp, size_t* rp,
                                 std::atomic<bool>* ce, std::atomic<LogLevel>* cl,
                                 std::atomic<LogLevel>* fl)
    : log_dir(std::move(log_dir)), processed_ptr(pp), retention_ptr(rp),
      console_enabled_ptr(ce), console_level_ptr(cl), file_level_ptr(fl)
{
    try {
        rotate();
        auto dir = fs::path(this->log_dir);
        run_file.open(dir / ("run_" + file_ts() + ".log"));
        latest_file.open(dir / "latest.log");
    } catch (const std::exception& e) {
        // Non-fatal: continue without file logging
    }
}

void Logger::FileHandler::rotate() {
    fs::path dir(log_dir);
    fs::create_directories(dir);

    std::vector<fs::path> runs;
    const std::string prefix = "run_";
    const std::string suffix = ".log";
    for (auto& e : fs::directory_iterator(dir)) {
        auto name = e.path().filename().string();
        if (name.size() > prefix.size() + suffix.size()
            && name.substr(0, prefix.size()) == prefix
            && name.substr(name.size() - suffix.size()) == suffix)
            runs.push_back(e.path());
    }

    std::sort(runs.begin(), runs.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() > b.filename().string();
              });

    while (runs.size() >= (retention_ptr ? *retention_ptr : 5)) {
        fs::remove(runs.back());
        runs.pop_back();
    }

    fs::remove(dir / "latest.log");
}

void Logger::FileHandler::operator()(LogEntry entry) {
    auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] "
              + entry.message + "\n";

    // File sinks are gated by the FILE threshold (BESQ_LOG_LEVEL); the console
    // mirror by its OWN threshold (BESQ_LOG_CONSOLE_LEVEL).  They are
    // independent — log() passes an entry through if either would show it.
    const auto file_lvl = file_level_ptr ? file_level_ptr->load(std::memory_order_acquire)
                                         : LogLevel::Debug;
    if (entry.level >= file_lvl) {
        if (run_file.is_open()) {
            run_file << line;
            run_file.flush();
        }
        if (latest_file.is_open()) {
            latest_file << line;
            latest_file.flush();
        }
    }

    // Console mirror: Warn/Error → stderr (diagnostics), Debug/Info → stdout.
    // Async, but flushed per-line; the Logger destructor drains on exit.
    if (console_enabled_ptr && console_enabled_ptr->load(std::memory_order_acquire)) {
        auto cl = console_level_ptr ? console_level_ptr->load(std::memory_order_acquire)
                                    : LogLevel::Warn;
        if (entry.level >= cl) {
            std::FILE* s = (entry.level >= LogLevel::Warn) ? stderr : stdout;
            std::fputs(line.c_str(), s);
            std::fflush(s);
        }
    }

    if (processed_ptr)
        processed_ptr->fetch_add(1, std::memory_order_release);
}

// ─── Singleton ────────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

// ─── Public API ───────────────────────────────────────────────────────

Logger::Logger(std::string log_dir)
    : _loop(FileHandler(std::move(log_dir), &_processed, &_max_retention,
                        &_console_enabled, &_console_level, &_level))
{
    // Console mirror defaults to enabled at Warn.  The host overrides via
    // AppConfig → setup_logger() (BESQ_LOG_CONSOLE / BESQ_LOG_CONSOLE_LEVEL);
    // the constructor does no env parsing of its own.
    _loop.start();
}

Logger::~Logger() {
    _loop.stop();  // graceful: drain remaining entries
}

void Logger::log(LogLevel level, std::string message) {
    // Drop only if NEITHER sink would show it: the file is gated by _level
    // (BESQ_LOG_LEVEL) and the console mirror by its own threshold
    // (BESQ_LOG_CONSOLE_LEVEL) — the two knobs are independent.
    const auto file_lvl    = _level.load(std::memory_order_acquire);
    const bool console_on  = _console_enabled.load(std::memory_order_acquire);
    const auto console_lvl = _console_level.load(std::memory_order_acquire);
    if (level < file_lvl && !(console_on && level >= console_lvl))
        return;
    if (_loop.try_post(LogEntry{level, std::move(message)})) {
        _enqueued.fetch_add(1, std::memory_order_release);
    }
}

void Logger::flush() {
    auto target = _enqueued.load(std::memory_order_acquire);
    while (_processed.load(std::memory_order_acquire) < target)
        std::this_thread::yield();
}
