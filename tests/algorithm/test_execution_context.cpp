#include "framework/test_utils.h"
#include "algorithm/ExecutionContext.h"
#include "types/CompactedTypes.h"
#include <chrono>
#include <thread>

void test_cancel() {
    ExecutionContext ctx;
    expect(!ctx.is_cancelled(), "initially not cancelled");
    ctx.cancel();
    expect(ctx.is_cancelled(), "cancelled after cancel()");
    std::cout << "PASS: test_cancel" << std::endl;
}

void test_pause_resume() {
    ExecutionContext ctx;
    expect(!ctx.is_paused(), "initially not paused");
    ctx.pause();
    expect(ctx.is_paused(), "paused after pause()");
    ctx.resume();
    expect(!ctx.is_paused(), "not paused after resume()");
    std::cout << "PASS: test_pause_resume" << std::endl;
}

void test_progress() {
    ExecutionContext ctx;
    ctx.report_progress(0.5, ProgressStatus::Exploring);
    expect(ctx.progress() == 0.5, "progress should be 0.5");
    std::cout << "PASS: test_progress" << std::endl;
}

void test_diagnostic_log() {
    ExecutionContext ctx;

    ctx.report_diagnostic("wall_ms", int64_t{583});
    ctx.report_diagnostic("status", std::string("Complete"));
    ctx.report_diagnostic("explored_count", int64_t{142378});

    auto log = ctx.consume_diagnostic_log();
    expect(log.size() == 3, "diagnostic log should have 3 entries");
    expect(ctx.consume_diagnostic_log().empty(), "log should be empty after consume");
    std::cout << "PASS: test_diagnostic_log" << std::endl;
}

void test_solution_accumulation() {
    ExecutionContext ctx;

    std::vector<compact::EnchStep> steps;
    steps.push_back(compact::EnchStep{{}, {}, 5});
    steps.push_back(compact::EnchStep{{}, {}, 3});
    ctx.report_compact_solution(std::move(steps));

    auto solutions = ctx.get_solutions();
    expect(solutions.size() == 1, "should accumulate one solution");
    expect(solutions[0].total_cost == 8, "total cost should be 8");
    std::cout << "PASS: test_solution_accumulation" << std::endl;
}

void test_wait_if_paused_resume() {
    ExecutionContext ctx;
    bool resumed = false;
    std::thread algo_thread([&] {
        ctx.pause();
        ctx.wait_if_paused();
        resumed = true;
    });

    // Poll until the context confirms pause, with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!ctx.is_paused() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(!resumed, "algo should be blocked on pause");
    ctx.resume();
    algo_thread.join();
    expect(resumed, "algo should have resumed");
    std::cout << "PASS: test_wait_if_paused_resume" << std::endl;
}

int main() {
    try {
        test_cancel();
        test_pause_resume();
        test_progress();
        test_diagnostic_log();
        test_solution_accumulation();
        test_wait_if_paused_resume();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
