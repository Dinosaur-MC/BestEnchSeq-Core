#include "log/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

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

// ─── Rotation ────────────────────────────────────────────────────────

void Logger::_rotate() {
    fs::path dir(_log_dir);
    fs::create_directories(dir);

    // Collect existing run_XXXX.log files
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

    // Sort by name (timestamp descending — most recent first)
    std::sort(runs.begin(), runs.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() > b.filename().string();
              });

    // Remove oldest beyond retention limit
    while (runs.size() >= _max_retention) {
        fs::remove(runs.back());
        runs.pop_back();
    }

    // Remove stale latest.log
    fs::remove(dir / "latest.log");
}

// ─── Worker thread ───────────────────────────────────────────────────

void Logger::_worker() {
    try {
        _rotate();
    } catch (const std::exception&) {
        // Rotate failures (permission, disk full) are non-fatal.
        // Worker continues without rotation — next run will try again.
    }

    auto dir = fs::path(_log_dir);
    auto run_path = dir / ("run_" + now_str() + ".log");
    auto latest_path = dir / "latest.log";

    std::ofstream run_file(run_path);
    std::ofstream latest_file(latest_path);

    LogEntry entry;
    while (_running.load(std::memory_order_acquire)) {
        // Drain all available entries (burst handling).
        while (_queue.try_pop(entry)) {
            auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] "
                      + entry.message + "\n";
            if (run_file.is_open()) {
                run_file << line;
                run_file.flush();
            }
            if (latest_file.is_open()) {
                latest_file << line;
                latest_file.flush();
            }
        }
        if (!_running.load(std::memory_order_acquire))
            break;

        // C++20 atomic::wait — zero CPU while idle, no mutex/CV.
        auto prev = _wake_seq.load(std::memory_order_acquire);
        if (_queue.try_pop(entry)) {
            auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] "
                      + entry.message + "\n";
            if (run_file.is_open()) run_file << line;
            if (latest_file.is_open()) latest_file << line;
            continue;
        }
        _wake_seq.wait(prev, std::memory_order_acquire);
    }

    // Drain remaining
    while (_queue.try_pop(entry)) {
        auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] "
                  + entry.message + "\n";
        if (run_file.is_open()) run_file << line;
        if (latest_file.is_open()) latest_file << line;
    }
}

// ─── Singleton ────────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

// ─── Public API ──────────────────────────────────────────────────────

Logger::Logger(std::string log_dir)
    : _log_dir(std::move(log_dir))
{
    _worker_thread = std::thread(&Logger::_worker, this);
}

Logger::~Logger() {
    _running.store(false, std::memory_order_release);
    _wake_seq.fetch_add(1, std::memory_order_release);
    _wake_seq.notify_all();
    if (_worker_thread.joinable())
        _worker_thread.join();
}

void Logger::log(LogLevel level, std::string message) {
    // Drop messages below the configured minimum level.
    if (level < _level.load(std::memory_order_acquire))
        return;
    if (_queue.try_push(LogEntry{level, std::move(message)})) {
        _wake_seq.fetch_add(1, std::memory_order_release);
        _wake_seq.notify_one();
    }
}

void Logger::flush() {
    _running.store(false, std::memory_order_release);
    _wake_seq.fetch_add(1, std::memory_order_release);
    _wake_seq.notify_all();
    if (_worker_thread.joinable())
        _worker_thread.join();
    _running.store(true, std::memory_order_release);
    _worker_thread = std::thread(&Logger::_worker, this);
}
