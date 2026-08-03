// =============================================================================
// Logger tests — async file logging, level filtering, rotation pruning.
//
// The Logger singleton is constructed on first instance() call with the default
// log_dir "logs" (CWD-relative).  We chdir to a fresh temp dir BEFORE the first
// instance() call so the files land there, and pre-create fake `run_*.log`
// files so the FileHandler constructor's rotate() must prune them.
// =============================================================================

#include "common/log/Logger.h"
#include "common/log/LogTypes.h"
#include "framework/test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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
        if (name.rfind("run_", 0) == 0 && name.size() > 8) ++n;
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
    expect(c.find("filtered_info_marker") == std::string::npos,
           "info dropped below Warn file level");
    expect(c.find("visible_warn_marker") != std::string::npos,
           "warn passes at Warn file level");
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
    for (auto& th : threads) th.join();
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

}  // anonymous namespace

int main() {
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
    logger.set_console_enabled(false);  // keep the console mirror quiet

    try {
        test_basic_logging(logger, cwd.dir());
        test_level_filtering(logger, cwd.dir());
        test_setters_roundtrip(logger);
        test_concurrent_writes(logger, cwd.dir());
        test_rotation_prunes(logger, cwd.dir());
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
