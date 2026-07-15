#include "framework/test_utils.h"
#include "algorithm/IAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include <chrono>
#include <stdexcept>
#include <thread>

// Empty AlgorithmInput for tests that don't need real data.
static AlgorithmInput g_test_input;

// ─── Test IAlgorithm implementation (compact-only) ───

class TestAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "test"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(const AlgorithmInput&, ExecutionContext& ctx) override {
        for (int i = 0; i < 5; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            ctx.report_progress((i + 1) * 20.0, ProgressStatus::Exploring);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::vector<compact::EnchStep> solution;
        solution.push_back(compact::EnchStep{{}, {}, 4});
        ctx.report_solution(solution);
    }
};

class SlowAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "slow"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(const AlgorithmInput&, ExecutionContext& ctx) override {
        for (int i = 0; i < 20; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};

// ─── Throwing algorithm for error-path test ───

class ThrowingAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "throwing"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(const AlgorithmInput&, ExecutionContext&) override {
        throw std::runtime_error("simulated failure");
    }
};

// ─── Tests ───

void test_constructor_null() {
    bool threw = false;
    try {
        AlgorithmExecutor executor(nullptr);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "null algorithm should throw");
    std::cout << "PASS: test_constructor_null" << std::endl;
}

void test_initial_state() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    expect(executor.state() == AlgorithmState::Idle, "initial state should be Idle");
    std::cout << "PASS: test_initial_state" << std::endl;
}

void test_executor_lifecycle() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    expect(executor.state() == AlgorithmState::Idle, "initial state should be Idle");

    executor.start(g_test_input);
    expect(executor.state() == AlgorithmState::Running, "state should be Running after start");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Completed, "should complete successfully");
    expect(executor.state() == AlgorithmState::Completed, "state should be Completed after wait");
    std::cout << "PASS: test_executor_lifecycle" << std::endl;
}

void test_double_start() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);

    bool threw = false;
    try {
        executor.start(g_test_input);
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "double start should throw std::logic_error");
    executor.wait();
    std::cout << "PASS: test_double_start" << std::endl;
}

void test_executor_cancel() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);

    // Poll until executor is actually running
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (executor.state() != AlgorithmState::Running &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(executor.state() == AlgorithmState::Running, "should be Running after start");

    executor.cancel();
    expect(executor.state() == AlgorithmState::Cancelled, "state should be Cancelled after cancel()");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Cancelled, "should be Cancelled after wait");
    std::cout << "PASS: test_executor_cancel" << std::endl;
}

void test_executor_pause_resume() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);

    // Poll until executor is running
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (executor.state() != AlgorithmState::Running &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    executor.pause();
    expect(executor.state() == AlgorithmState::Paused, "should be Paused after pause()");

    // Brief poll to confirm it stays paused
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    expect(executor.state() == AlgorithmState::Paused, "should remain Paused while paused");

    executor.resume();
    expect(executor.state() == AlgorithmState::Running, "should be Running after resume()");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Completed, "should complete after resume");
    std::cout << "PASS: test_executor_pause_resume" << std::endl;
}

void test_executor_progress() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);
    executor.wait();

    expect(executor.progress() > 0, "executor.progress() should return > 0 after execution");
    std::cout << "PASS: test_executor_progress" << std::endl;
}

void test_output_not_valid_before_completion() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);

    auto out = executor.output();
    expect(!out.is_valid, "output should not be valid while running");

    executor.cancel();
    executor.wait();
    std::cout << "PASS: test_output_not_valid_before_completion" << std::endl;
}

void test_output_has_steps_after_completion() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");
    auto out = executor.output();
    expect(out.is_valid, "output should be valid after completion");
    expect(!out.solutions.empty(), "output solutions should be populated after completion");
    expect(out.solutions.size() == 1, "should have one solution");
    expect(out.solutions[0].steps.size() == 1, "solution should have one step");
    expect(out.solutions[0].steps[0].cost == 4, "step cost should match reported value");
    std::cout << "PASS: test_output_has_steps_after_completion" << std::endl;
}

void test_serialization_stubs() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    auto data = executor.serialize_state();
    expect(data.empty(), "serialize_state should return empty");

    bool restored = executor.restore_state({});
    expect(!restored, "restore_state should return false");
    std::cout << "PASS: test_serialization_stubs" << std::endl;
}

void test_executor_throwing_algorithm() {
    auto algo = std::make_unique<ThrowingAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Failed,
           "throwing algorithm should result in Failed state");
    expect(executor.state() == AlgorithmState::Failed,
           "state should be Failed after throwing algorithm");
    std::cout << "PASS: test_executor_throwing_algorithm" << std::endl;
}

int main() {
    try {
        test_constructor_null();
        test_initial_state();
        test_executor_lifecycle();
        test_double_start();
        test_executor_cancel();
        test_executor_pause_resume();
        test_executor_progress();
        test_output_not_valid_before_completion();
        test_output_has_steps_after_completion();
        test_serialization_stubs();
        test_executor_throwing_algorithm();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
