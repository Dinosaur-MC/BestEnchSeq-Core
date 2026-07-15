#include "framework/test_utils.h"
#include "algorithm/ExecutionContext.h"
#include "types/CompactedTypes.h"
#include <chrono>
#include <thread>
#include <atomic>

void test_cancel() {
    ExecutionContext ctx(1, "test");
    expect(!ctx.is_cancelled(), "initially not cancelled");
    ctx.cancel();
    expect(ctx.is_cancelled(), "cancelled after cancel()");
    std::cout << "PASS: test_cancel" << std::endl;
}

void test_pause_resume() {
    ExecutionContext ctx(1, "test");
    std::atomic<bool> paused{false};
    bool resumed = false;
    std::thread t([&] {
        ctx.pause();
        paused.store(true, std::memory_order_release);
        ctx.wait_if_paused();
        resumed = true;
    });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!paused.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(!resumed, "should be blocked on pause");
    ctx.resume();
    t.join();
    expect(resumed, "should have resumed");
    std::cout << "PASS: test_pause_resume" << std::endl;
}

void test_progress() {
    ExecutionContext ctx(1, "test");
    ctx.report_progress(0.5, ProgressStatus::Exploring);
    expect(ctx.progress() == 0.5, "progress should be 0.5");
    std::cout << "PASS: test_progress" << std::endl;
}

void test_diagnostic_log() {
    ExecutionContext ctx(1, "test");

    auto diag = std::make_unique<SearchDiagnostics>();
    diag->algorithm_name = "AStar";
    diag->status = "Complete";
    diag->solution_cost = 42;

    expect(ctx.consume_exit_diagnostics() == nullptr, "no diagnostics initially");
    ctx.set_exit_diagnostics(std::move(diag));

    auto retrieved = ctx.consume_exit_diagnostics();
    expect(retrieved != nullptr, "should retrieve diagnostics");
    expect(retrieved->algorithm_name == "AStar", "algorithm_name should be AStar");
    expect(retrieved->status == "Complete", "status should be Complete");
    expect(retrieved->solution_cost == 42, "solution_cost should be 42");
    expect(ctx.consume_exit_diagnostics() == nullptr, "no diagnostics after consume");
    std::cout << "PASS: test_diagnostic_log" << std::endl;
}

void test_solution_accumulation() {
    ExecutionContext ctx(1, "test");

    std::vector<compact::EnchStep> steps;
    steps.push_back(compact::EnchStep{{}, {}, 5});
    steps.push_back(compact::EnchStep{{}, {}, 3});
    ctx.report_solution(std::move(steps));

    auto solutions = ctx.get_solutions();
    expect(solutions.size() == 1, "should accumulate one solution");
    expect(solutions[0].total_cost == 8, "total cost should be 8");
    std::cout << "PASS: test_solution_accumulation" << std::endl;
}

void test_wait_if_paused_resume() {
    ExecutionContext ctx(1, "test");
    std::atomic<bool> paused{false};
    bool resumed = false;
    std::thread algo_thread([&] {
        ctx.pause();
        paused.store(true, std::memory_order_release);
        ctx.wait_if_paused();
        resumed = true;
    });

    // Poll until the thread confirms pause, with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!paused.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
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
