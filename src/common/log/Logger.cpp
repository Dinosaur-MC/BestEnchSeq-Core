#include "Logger.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
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
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "????";
}

// ─── Consumer chain ──────────────────────────────────────────────────────

Logger::ConsumerId Logger::add_consumer(std::function<void(const LogEntry&)> consumer) {
    std::lock_guard<std::mutex> lock(_chain.mutex);
    const ConsumerId id = ++_chain.next_id;
    _chain.consumers.emplace_back(id, std::move(consumer));
    return id;
}

void Logger::remove_consumer(ConsumerId id) {
    std::lock_guard<std::mutex> lock(_chain.mutex);
    auto& v = _chain.consumers;
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->first == id) {
            v.erase(it);
            return;
        }
    }
}

void Logger::ChainHandler::operator()(LogEntry entry) {
    // 锁内拷贝快照、锁外链式调用：慢消费者不阻塞注册/移除，注册/移除也不
    // 阻塞在途调用。每个消费者独立 try/catch——一个坏了不中断链。
    std::vector<std::pair<ConsumerId, ConsumerFn>> snapshot;
    {
        std::lock_guard<std::mutex> lock(chain->mutex);
        snapshot = chain->consumers;
    }
    for (const auto& [id, fn] : snapshot) {
        try {
            fn(entry);
        } catch (...) {
            // 消费者抛异常 → 捕获丢弃，链继续
        }
    }
    if (processed_ptr)
        processed_ptr->fetch_add(1, std::memory_order_release);
}

// ─── Console consumer ────────────────────────────────────────────────────
// Console mirror: Warn/Error → stderr (diagnostics), Debug/Info → stdout.
// Async, but flushed per-line; the Logger destructor drains on exit.

void Logger::ConsoleConsumer::operator()(const LogEntry& entry) const {
    // The sync path (LOG_* / log_sync) already printed this entry directly —
    // skip it here so it is never printed twice (the FileConsumer still
    // receives it for the async file sink).
    if (entry.console_printed)
        return;
    if (!console_enabled_ptr || !console_enabled_ptr->load(std::memory_order_acquire))
        return;
    const auto cl = console_level_ptr ? console_level_ptr->load(std::memory_order_acquire) : LogLevel::Warn;
    if (entry.level < cl)
        return;
    auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] " + entry.message + "\n";
    std::FILE* s = (entry.level >= LogLevel::Warn) ? stderr : stdout;
    std::fputs(line.c_str(), s);
    std::fflush(s);
}

// ─── File consumer ───────────────────────────────────────────────────────

Logger::FileConsumer::FileConsumer(std::string log_dir, size_t* rp, std::atomic<LogLevel>* fl)
    : log_dir(std::move(log_dir)), retention_ptr(rp), file_level_ptr(fl) {
    try {
        rotate();
        auto dir = fs::path(this->log_dir);
        run_file.open(dir / ("run_" + file_ts() + ".log"));
        latest_file.open(dir / "latest.log");
    } catch (const std::exception& e) {
        // Non-fatal: continue without file logging
    }
}

void Logger::FileConsumer::rotate() {
    fs::path dir(log_dir);
    fs::create_directories(dir);

    std::vector<fs::path> runs;
    const std::string prefix = "run_";
    const std::string suffix = ".log";
    for (auto& e : fs::directory_iterator(dir)) {
        auto name = e.path().filename().string();
        if (name.size() > prefix.size() + suffix.size() && name.substr(0, prefix.size()) == prefix &&
            name.substr(name.size() - suffix.size()) == suffix)
            runs.push_back(e.path());
    }

    std::sort(runs.begin(), runs.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename().string() > b.filename().string(); });

    while (runs.size() >= (retention_ptr ? *retention_ptr : 5)) {
        fs::remove(runs.back());
        runs.pop_back();
    }

    fs::remove(dir / "latest.log");
}

void Logger::FileConsumer::operator()(const LogEntry& entry) {
    // File sink is gated by the FILE threshold (BESQ_LOG_LEVEL); the console
    // mirror has its OWN threshold — the two knobs are independent.
    const auto file_lvl = file_level_ptr ? file_level_ptr->load(std::memory_order_acquire) : LogLevel::Debug;
    if (entry.level < file_lvl)
        return;
    auto line = "[" + now_str() + "] [" + level_str(entry.level) + "] " + entry.message + "\n";
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
/*
struct LoggerConfig {
    int32_t level           = 0;    // file-log threshold
    size_t  retention       = 5;    // max historic log files kept during rotation
    bool    console_enabled = true; // mirror to stderr (Warn/Error) / stdout (Debug/Info)
    int32_t console_level   = 2;    // console mirror threshold
    std::string log_dir     = "logs";
};
*/
Logger::Logger(const LoggerConfig& cfg)
    : _max_retention(cfg.retention), _console_enabled(cfg.console_enabled),
      _console_level(static_cast<LogLevel>(cfg.console_level)), _level(static_cast<LogLevel>(cfg.level)) {
    // 内建消费者在 worker 启动前注册（注册即生效）：控制台镜像与落盘是两个
    // 独立消费者，阈值/门控语义与拆分前的 FileHandler 一致。控制台消费者走
    // 成员 _console（同步 log_sync 路径共用同一实例/门控）；FileConsumer
    // 持有 move-only 的 ofstream——经 shared_ptr 捕获进 std::function。
    // Console mirror defaults to enabled at Warn.  The host overrides via
    // AppConfig → setup_logger() (BESQ_LOG_CONSOLE / BESQ_LOG_CONSOLE_LEVEL);
    // the constructor does no env parsing of its own.
    add_consumer([this](const LogEntry& e) { _console(e); });
    add_consumer([fc = std::make_shared<FileConsumer>(cfg.log_dir, &_max_retention, &_level)](const LogEntry& e) { (*fc)(e); });
    _loop.start();
}

Logger::~Logger() {
    _loop.stop(); // graceful: drain remaining entries
}

void Logger::log(LogLevel level, std::string message) {
    // log() 恒入队——过滤是各消费者的职责（"都不显示则 drop"的预过滤删除）；
    // 队列满时 try_post 失败自然丢弃。enqueued 只计成功入队条目，flush 语义不变。
    if (_loop.try_post(LogEntry{level, std::move(message), /*console_printed=*/false})) {
        _enqueued.fetch_add(1, std::memory_order_release);
    }
}

void Logger::log_sync(LogLevel level, std::string message) {
    // SYNC console: print immediately (gated by console_enabled/console_level
    // inside ConsoleConsumer::operator()); the file sink stays ASYNC — the
    // entry is enqueued with console_printed=true so the worker's console
    // consumer skips it (no double print) while the FileConsumer writes it.
    LogEntry entry{level, message, /*console_printed=*/true};
    _console(entry);
    if (_loop.try_post(LogEntry{level, std::move(message), /*console_printed=*/true})) {
        _enqueued.fetch_add(1, std::memory_order_release);
    }
}

void Logger::flush() {
    auto target = _enqueued.load(std::memory_order_acquire);
    while (_processed.load(std::memory_order_acquire) < target)
        std::this_thread::yield();
}
