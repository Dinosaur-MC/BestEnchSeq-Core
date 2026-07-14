#include "log/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
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

Logger::FileHandler::FileHandler(std::string log_dir)
    : log_dir(std::move(log_dir))
{
    // Open files in constructor (called before worker thread starts).
    rotate();
    auto dir = fs::path(this->log_dir);
    run_file.open(dir / ("run_" + file_ts() + ".log"));
    latest_file.open(dir / "latest.log");
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

    while (runs.size() >= 5) {
        fs::remove(runs.back());
        runs.pop_back();
    }

    fs::remove(dir / "latest.log");
}

void Logger::FileHandler::operator()(LogEntry entry) {
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

// ─── Singleton ────────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

// ─── Public API ───────────────────────────────────────────────────────

Logger::Logger(std::string log_dir)
    : _loop(FileHandler(std::move(log_dir)))
{
    _loop.start();
}

Logger::~Logger() {
    _loop.stop();  // graceful: drain remaining entries
}

void Logger::log(LogLevel level, std::string message) {
    if (level < _level.load(std::memory_order_acquire))
        return;
    _loop.try_post(LogEntry{level, std::move(message)});
}

void Logger::flush() {
    while (!_loop.empty())
        std::this_thread::yield();
}
