#include "test_utils.h"
#include "algorithm/IAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/forge/ForgeEngine.h"
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include <thread>

// ─── Test IAlgorithm implementation (compact-only) ───

class TestAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "test"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>&,
        const compact::EnchReg&,
        const std::vector<compact::Ench>&,
        ExecutionContext& ctx
    ) override {
        for (int i = 0; i < 5; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            ctx.report_progress((i + 1) * 20.0, ProgressStatus::Exploring);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::vector<compact::EnchStep> solution;
        solution.push_back(compact::EnchStep{{}, {}, 4});
        ctx.report_compact_solution(std::move(solution));
    }
};

class SlowAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "slow"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>&,
        const compact::EnchReg&,
        const std::vector<compact::Ench>&,
        ExecutionContext& ctx
    ) override {
        for (int i = 0; i < 20; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};

// ─── Observers (compact step types) ───

class TestSolutionObserver : public AlgorithmObserver {
public:
    int found_count = 0;
    void on_solution_found(const std::vector<compact::EnchStep>&) override { ++found_count; }
};

class TestProgressObserver : public AlgorithmObserver {
public:
    double last_progress = 0.0;
    void on_progress(double percent, ProgressStatus) override { last_progress = percent; }
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

    executor.start(AlgorithmInput{});
    expect(executor.state() == AlgorithmState::Running, "state should be Running after start");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Completed, "should complete successfully");
    expect(executor.state() == AlgorithmState::Completed, "state should be Completed after wait");
    std::cout << "PASS: test_executor_lifecycle" << std::endl;
}

void test_double_start() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(AlgorithmInput{});

    bool threw = false;
    try {
        executor.start(AlgorithmInput{});
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

    executor.start(AlgorithmInput{});
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

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

    executor.start(AlgorithmInput{});
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    executor.pause();
    expect(executor.state() == AlgorithmState::Paused, "should be Paused after pause()");
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

    auto progress_obs = std::make_shared<TestProgressObserver>();
    executor.attach_observer(progress_obs);

    executor.start(AlgorithmInput{});
    executor.wait();

    expect(progress_obs->last_progress > 0, "progress should be reported");
    expect(executor.progress() > 0, "executor.progress() should return > 0 after execution");
    std::cout << "PASS: test_executor_progress" << std::endl;
}

void test_executor_observer() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    auto obs = std::make_shared<TestSolutionObserver>();
    executor.attach_observer(obs);

    executor.start(AlgorithmInput{});
    executor.wait();

    expect(obs->found_count >= 1, "observer should have been called for solution");
    std::cout << "PASS: test_executor_observer" << std::endl;
}

void test_executor_detach_observer() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    auto obs = std::make_shared<TestSolutionObserver>();
    executor.attach_observer(obs);
    executor.detach_observer(obs);

    executor.start(AlgorithmInput{});
    executor.wait();

    expect(obs->found_count == 0, "detached observer should not be called");
    std::cout << "PASS: test_executor_detach_observer" << std::endl;
}

void test_output_not_valid_before_completion() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(AlgorithmInput{});

    auto out = executor.output();
    expect(!out.is_valid, "output should not be valid while running");

    executor.cancel();
    executor.wait();
    std::cout << "PASS: test_output_not_valid_before_completion" << std::endl;
}

void test_output_has_steps_after_completion() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(AlgorithmInput{});
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");
    auto out = executor.output();
    expect(out.is_valid, "output should be valid after completion");
    expect(!out.steps.empty(), "output steps should be populated after completion");
    expect(out.steps.size() == 1, "should have one solution");
    expect(out.steps[0].size() == 1, "solution should have one step");
    expect(out.steps[0][0].cost == 4, "step cost should match reported value");
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

int main() {
    test_constructor_null();
    test_initial_state();
    test_executor_lifecycle();
    test_double_start();
    test_executor_cancel();
    test_executor_pause_resume();
    test_executor_progress();
    test_executor_observer();
    test_executor_detach_observer();
    test_output_not_valid_before_completion();
    test_output_has_steps_after_completion();
    test_serialization_stubs();
    std::cout << "All AlgorithmExecutor tests passed!" << std::endl;
    return 0;
}
