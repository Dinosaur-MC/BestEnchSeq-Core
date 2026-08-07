// =============================================================================
// Logger tests — async file logging, level filtering, rotation pruning.
//
// The Logger singleton is constructed on first instance() call with the default
// log_dir "logs" (CWD-relative).  We chdir to a fresh temp dir BEFORE the first
// instance() call so the files land there, and pre-create fake `run_*.log`
// files so the FileHandler constructor's rotate() must prune them.
// =============================================================================

#define BESQ_TEST_MAIN
#include "common/log/Logger.h"
#include "common/log/LogRingBuffer.h"
#include "common/log/LogTypes.h"
#include "framework/test_framework.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_cwd_counter = 0;

class CwdGuard {
public:
    CwdGuard() {
        _orig = fs::current_path();
        _tmp = fs::temp_directory_path() / ("besq_log_test_" + std::to_string(++g_cwd_counter));
        fs::remove_all(_tmp);
        fs::create_directories(_tmp);
        fs::current_path(_tmp);
    }
    ~CwdGuard() {
        // Restore CWD; the log files may still be held open by the Logger
        // singleton's worker (destroyed after main), so removal can fail on
        // Windows — that is fine (harmless temp litter, no test impact).
        std::error_code ec;
        fs::current_path(_orig, ec);
        fs::remove_all(_tmp, ec);
    }
    const fs::path& dir() const { return _tmp; }

private:
    fs::path _orig, _tmp;
};

std::string read_latest(const fs::path& log_dir) {
    std::ifstream f(log_dir / "logs" / "latest.log");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

size_t count_run_files(const fs::path& log_dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(log_dir / "logs", ec)) {
        const auto name = e.path().filename().string();
        if (name.rfind("run_", 0) == 0 && name.size() > 8)
            ++n;
    }
    return n;
}

void test_basic_logging(Logger& logger, const fs::path& log_dir) {
    logger.set_level(LogLevel::Debug);
    logger.info("marker_info");
    logger.warn("marker_warn");
    logger.error("marker_error");
    logger.debug("marker_debug");
    logger.flush();

    auto c = read_latest(log_dir);
    expect(c.find("marker_info") != std::string::npos, "latest.log has info line");
    expect(c.find("[INFO]") != std::string::npos, "latest.log tags INFO");
    expect(c.find("marker_warn") != std::string::npos, "latest.log has warn line");
    expect(c.find("[WARN]") != std::string::npos, "latest.log tags WARN");
    expect(c.find("marker_error") != std::string::npos, "latest.log has error line");
    expect(c.find("marker_debug") != std::string::npos, "latest.log has debug line");
    TEST_PASS("logger: async file write at Debug level");
}

void test_level_filtering(Logger& logger, const fs::path& log_dir) {
    logger.set_level(LogLevel::Warn);
    logger.info("filtered_info_marker");
    logger.warn("visible_warn_marker");
    logger.flush();

    auto c = read_latest(log_dir);
    expect(c.find("filtered_info_marker") == std::string::npos, "info dropped below Warn file level");
    expect(c.find("visible_warn_marker") != std::string::npos, "warn passes at Warn file level");
    TEST_PASS("logger: file-level filtering");
}

void test_setters_roundtrip(Logger& logger) {
    logger.set_level(LogLevel::Error);
    expect(logger.get_level() == LogLevel::Error, "set/get file level round-trip");
    logger.set_retention(3);
    expect(logger.get_retention() == 3, "set/get retention round-trip");
    logger.set_console_enabled(true);
    expect(logger.console_enabled(), "set/get console enabled round-trip");
    logger.set_console_level(LogLevel::Error);
    expect(logger.console_level() == LogLevel::Error, "set/get console level round-trip");
    TEST_PASS("logger: setter/getter round-trips");
}

// LogRingBuffer live listeners (I-1): add_listener/remove_listener + push
// notification. ring->push is called synchronously on the log() caller's
// thread, so listener invocations are directly observable.
void test_ring_listeners() {
    auto ring = std::make_shared<LogRingBuffer>(8);

    // Backward compat: no listeners → snapshot/clear unchanged.
    ring->push(LogLevel::Info, "seed");
    auto snap = ring->snapshot(LogLevel::Debug, 100);
    expect(snap.size() == 1 && snap[0].message == "seed", "snapshot unchanged without listeners");

    std::vector<std::string> got1, got2;
    auto id1 = ring->add_listener([&](const LogRecord& e) { got1.push_back(e.message); });
    auto id2 = ring->add_listener([&](const LogRecord& e) { got2.push_back(e.message); });

    // 未知 id 移除是无操作（不误伤已有监听器）。
    ring->remove_listener(9999);

    ring->push(LogLevel::Warn, "live-a");
    expect(got1.size() == 1 && got1[0] == "live-a", "listener 1 notified on push");
    expect(got2.size() == 1 && got2[0] == "live-a", "listener 2 notified on push");

    // 记录字段：level/timestamp/message 完整传递。
    auto got = ring->snapshot(LogLevel::Debug, 100);
    expect(got.size() >= 2, "ring retains pushed records");
    expect(got.back().level == LogLevel::Warn && got.back().message == "live-a", "record level+message preserved");
    expect(got.back().timestamp_ms > 0, "record timestamp populated");

    // 移除后不再通知。
    ring->remove_listener(id1);
    ring->push(LogLevel::Info, "live-b");
    expect(got1.size() == 1, "removed listener no longer notified");
    expect(got2.size() == 2 && got2[1] == "live-b", "remaining listener still notified");

    // clear() 不动监听器（仅清缓冲）。
    ring->clear();
    expect(ring->snapshot(LogLevel::Debug, 100).empty(), "clear empties the buffer");
    ring->push(LogLevel::Info, "live-c");
    expect(got2.size() == 3, "listener survives clear()");

    ring->remove_listener(id2);
    ring->remove_listener(id2); // 幂等
    TEST_PASS("logger: ring buffer listeners");
}

void test_concurrent_writes(Logger& logger, const fs::path& log_dir) {
    logger.set_level(LogLevel::Debug);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&logger, t] {
            for (int i = 0; i < 40; ++i)
                logger.info("conc_" + std::to_string(t) + "_" + std::to_string(i));
        });
    }
    for (auto& th : threads)
        th.join();
    logger.flush();

    auto c = read_latest(log_dir);
    expect(c.find("conc_0_0") != std::string::npos, "concurrent writer 0 first line present");
    expect(c.find("conc_3_39") != std::string::npos, "concurrent writer 3 last line present");
    TEST_PASS("logger: concurrent producers");
}

void test_rotation_prunes(Logger& logger, const fs::path& log_dir) {
    // 8 fake `run_*.log` files were pre-created before Logger::instance(); the
    // FileHandler ctor's rotate() must have pruned them down to the retention
    // bound (5) plus the freshly opened current run (6 total).
    auto n = count_run_files(log_dir);
    expect(n < 8, "rotation pruned the pre-existing run files");
    expect(n >= 1 && n <= 6, "rotation kept the retention bound + current run");
    TEST_PASS("logger: rotation pruning");
}

} // anonymous namespace

TEST_CASE("test_logger") {
    CwdGuard cwd;

    // Pre-create 8 fake historic runs BEFORE the first instance() call so the
    // singleton's FileHandler constructor is forced to prune them.
    {
        fs::create_directories(cwd.dir() / "logs");
        for (int i = 0; i < 8; ++i) {
            std::ofstream f(cwd.dir() / "logs" / ("run_fake_" + std::to_string(i) + ".log"));
            f << "old\n";
        }
    }

    auto& logger = Logger::instance();
    logger.set_console_enabled(false); // keep the console mirror quiet

    test_basic_logging(logger, cwd.dir());
    test_level_filtering(logger, cwd.dir());
    test_setters_roundtrip(logger);
    test_concurrent_writes(logger, cwd.dir());
    test_rotation_prunes(logger, cwd.dir());
    test_ring_listeners();
}
