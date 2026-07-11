#include "test_utils.h"
#include "algorithm/AlgorithmObserver.h"
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

struct TestSolutionObserver : AlgorithmObserver {
    int found_count = 0;
    void on_solution_found(const std::vector<compact::EnchStep>&) override { ++found_count; }
};

void test_observer_solution() {
    ExecutionContext ctx;
    auto obs = std::make_shared<TestSolutionObserver>();
    ctx.attach_observer(obs);

    std::vector<compact::EnchStep> steps;
    steps.push_back(compact::EnchStep{{}, {}, 4});
    ctx.report_compact_solution(std::move(steps));
    ctx.dispatch_events();
    expect(obs->found_count == 1, "observer should be called once");
    ctx.detach_observer(obs);
    ctx.report_compact_solution(std::move(steps));
    ctx.dispatch_events();
    expect(obs->found_count == 1, "observer should not be called after detach");
    std::cout << "PASS: test_observer_solution" << std::endl;
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
        test_observer_solution();
        test_wait_if_paused_resume();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
