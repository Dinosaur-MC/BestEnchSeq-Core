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
#include "common/log/LogTypes.h"
#include "common/log/log.hpp"
#include "framework/test_framework.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
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
}

// 消费者链：注册即生效、worker 线程串行调用、坏消费者不中断链、移除后不再接收。
TEST_CASE("test_logger_consumers") {
    // 捕获消费者（worker 线程写、本线程读——用一把小 mutex 消除数据竞争）。
    std::mutex cap_mtx;
    std::vector<std::string> got;
    auto cid = Logger::instance().add_consumer([&](const LogEntry& e) {
        std::lock_guard<std::mutex> lk(cap_mtx);
        got.push_back(e.message);
    });
    // 坏消费者：每条都抛——链必须继续（消费者异常被捕获丢弃）。
    auto bad_id = Logger::instance().add_consumer([](const LogEntry&) { throw std::runtime_error("boom"); });

    // 注册即生效：info 一条标记消息 → 有界等待（≤2s）→ 断言收到。
    Logger::instance().info("consumer-chain-marker");
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        {
            std::lock_guard<std::mutex> lk(cap_mtx);
            for (const auto& m : got)
                if (m.find("consumer-chain-marker") != std::string::npos) {
                    found = true;
                    break;
                }
        }
        if (!found)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(found, "consumer receives entries (bounded wait)");

    // 坏消费者未中断链：坏消费者注册于捕获消费者之后——若链中断，上面的标记
    // 就传不到捕获消费者（已断言）；再显式发一条验证链仍完整。
    Logger::instance().info("consumer-chain-marker-2");
    Logger::instance().flush(); // worker 串行处理完毕
    {
        std::lock_guard<std::mutex> lk(cap_mtx);
        bool second = false;
        for (const auto& m : got)
            if (m.find("consumer-chain-marker-2") != std::string::npos) {
                second = true;
                break;
            }
        expect(second, "chain continues after bad consumer throws");
    }

    // 移除捕获消费者 → 不再收到（remove 前 flush 排空，无在途调用）。
    Logger::instance().flush();
    Logger::instance().remove_consumer(cid);
    Logger::instance().remove_consumer(bad_id);
    Logger::instance().info("consumer-chain-marker-3");
    Logger::instance().flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // worker 异步兜底
    {
        std::lock_guard<std::mutex> lk(cap_mtx);
        bool after = false;
        for (const auto& m : got)
            if (m.find("consumer-chain-marker-3") != std::string::npos) {
                after = true;
                break;
            }
        expect(!after, "removed consumer no longer receives entries");
    }
    TEST_PASS("test_logger_consumers");
}

// ─── SYNC console + async file (LOG_* vs LOG_*_ASYNC) ────────────────────
// The sync path prints the console line IMMEDIATELY and enqueues the entry
// with console_printed=true so the worker's ConsoleConsumer skips it (no
// double print) while the FileConsumer still writes it.  The async path
// enqueues with console_printed=false.  Assert via a capture consumer —
// the console print itself is gated by the same flags the consumer sees.

TEST_CASE("test_logger_sync_async_paths") {
    auto& logger = Logger::instance();
    logger.set_console_enabled(false); // keep the console mirror quiet
    std::mutex cap_mtx;
    std::vector<LogEntry> got;
    auto cid = logger.add_consumer([&](const LogEntry& e) {
        std::lock_guard<std::mutex> lk(cap_mtx);
        got.push_back(e);
    });

    LOG_INFO("sync-marker");          // LOG_* → sync console + async file
    LOG_WARN("sync-warn-marker");
    LOG_INFO_ASYNC("async-marker");   // LOG_*_ASYNC → fully async
    besq::log::flush();
    // 有界等待：worker 处理完（flush 已排空；再兜底等待捕获到达）。
    // NOTE: match with a leading space (" sync-marker") — "async-marker"
    // contains "sync-marker" as a substring and would otherwise overwrite
    // the sync flags with the async entry's console_printed.
    bool sync_seen = false, warn_seen = false, async_seen = false;
    bool sync_printed = false, warn_printed = false, async_printed = true;
    for (int i = 0; i < 200 && !(sync_seen && warn_seen && async_seen); ++i) {
        {
            std::lock_guard<std::mutex> lk(cap_mtx);
            for (const auto& e : got) {
                if (e.message.find(" sync-marker") != std::string::npos) {
                    sync_seen = true;
                    sync_printed = e.console_printed;
                }
                if (e.message.find("sync-warn-marker") != std::string::npos) {
                    warn_seen = true;
                    warn_printed = e.console_printed;
                }
                if (e.message.find("async-marker") != std::string::npos) {
                    async_seen = true;
                    async_printed = e.console_printed;
                }
            }
        }
        if (!(sync_seen && warn_seen && async_seen))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(sync_seen, "sync LOG_* entry reaches the consumer chain (file sink)");
    expect(sync_printed, "sync entry carries console_printed (console already emitted)");
    expect(warn_seen && warn_printed, "sync WARN entry carries console_printed");
    expect(async_seen, "async LOG_*_ASYNC entry reaches the consumer chain");
    expect(!async_printed, "async entry does NOT carry console_printed");

    // 宏级验证：LOG_*（同步）与 LOG_*_ASYNC（异步）都在 BESQ_DISABLE_LOGGER
    // 未定义时解析；标志语义已由上方断言覆盖。
    LOG_DEBUG("sync-debug-marker");
    LOG_DEBUG_ASYNC("async-debug-marker");
    besq::log::flush();

    logger.remove_consumer(cid);
    TEST_PASS("logger sync (console immediate) + async (queue) paths");
}
