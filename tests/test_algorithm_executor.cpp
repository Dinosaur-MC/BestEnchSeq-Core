#include "test_utils.h"
#include "algorithm/IAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/DefaultForgeEngine.h"
#include <thread>

// ─── Test IAlgorithm implementations ───

class TestAlgorithm : public IAlgorithm {
public:
    TestAlgorithm() : _engine(ForgeConfig{}) {}
    std::string_view name() const noexcept override { return "test"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _engine; }

    void execute(const AlgorithmInput&, ExecutionContext& ctx) override {
        for (int i = 0; i < 5; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            ctx.report_progress((i + 1) * 20.0, "working");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        EnchStepList solution;
        solution.push_back({{}, {}, 4, 9});
        ctx.report_solution_found(solution);
    }

private:
    DefaultForgeEngine _engine;
};

class SlowAlgorithm : public IAlgorithm {
public:
    SlowAlgorithm() : _engine(ForgeConfig{}) {}
    std::string_view name() const noexcept override { return "slow"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _engine; }
    void execute(const AlgorithmInput&, ExecutionContext& ctx) override {
        for (int i = 0; i < 20; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

private:
    DefaultForgeEngine _engine;
};

// ─── Test observer (derived class, not lambda assignment) ───

class TestSolutionObserver : public AlgorithmObserver {
public:
    int found_count = 0;
    void on_solution_found(const EnchStepList&) override { ++found_count; }
};

class TestProgressObserver : public AlgorithmObserver {
public:
    double last_progress = 0.0;
    void on_progress(double percent, std::string_view) override { last_progress = percent; }
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

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    expect(executor.state() == AlgorithmState::Running, "state should be Running after start");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Completed, "should complete successfully");
    expect(executor.state() == AlgorithmState::Completed, "state should be Completed after wait");
    std::cout << "PASS: test_executor_lifecycle" << std::endl;
}

void test_double_start() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);

    bool threw = false;
    try {
        executor.start(input);
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

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Verify it started
    expect(executor.state() == AlgorithmState::Running, "should be Running after start");

    executor.cancel();
    // After cancel, state should be Cancelled immediately
    expect(executor.state() == AlgorithmState::Cancelled, "state should be Cancelled after cancel()");

    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Cancelled, "should be Cancelled after wait");
    std::cout << "PASS: test_executor_cancel" << std::endl;
}

void test_executor_pause_resume() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    executor.pause();
    expect(executor.state() == AlgorithmState::Paused, "should be Paused after pause()");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Should still be paused after waiting
    expect(executor.state() == AlgorithmState::Paused, "should remain Paused while paused");

    executor.resume();
    expect(executor.state() == AlgorithmState::Running, "should be Running after resume()");

    // Wait for completion
    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Completed, "should complete after resume");
    std::cout << "PASS: test_executor_pause_resume" << std::endl;
}

void test_executor_progress() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    // Attach progress observer before start
    auto progress_obs = std::make_shared<TestProgressObserver>();
    executor.attach_observer(progress_obs);

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    executor.wait();

    // Progress should have been reported
    expect(progress_obs->last_progress > 0, "progress should be reported");
    expect(executor.progress() > 0, "executor.progress() should return > 0 after execution");
    std::cout << "PASS: test_executor_progress" << std::endl;
}

void test_executor_observer() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    auto obs = std::make_shared<TestSolutionObserver>();
    executor.attach_observer(obs);

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
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

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    executor.wait();

    // Observer was detached before start, so should not be called
    expect(obs->found_count == 0, "detached observer should not be called");
    std::cout << "PASS: test_executor_detach_observer" << std::endl;
}

void test_output_not_valid_before_completion() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);

    // Output should not be valid while running
    auto out = executor.output();
    expect(!out.is_valid, "output should not be valid while running");

    executor.cancel();
    executor.wait();
    std::cout << "PASS: test_output_not_valid_before_completion" << std::endl;
}

void test_output_has_steps_after_completion() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    AlgorithmInput input{platform::MCE::Java, {}, {}, {}};
    executor.start(input);
    executor.wait();

    expect(executor.state() == AlgorithmState::Completed, "should complete");
    auto out = executor.output();
    expect(out.is_valid, "output should be valid after completion");
    expect(!out.steps.empty(), "output steps should be populated after completion");
    expect(out.steps.size() == 1, "should have one solution");
    expect(out.steps[0].size() == 1, "solution should have one step");
    expect(out.steps[0][0].exp_level_cost == 4, "step cost should match reported value");
    std::cout << "PASS: test_output_has_steps_after_completion" << std::endl;
}

void test_serialization_stubs() {
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    // Serialization stubs should return defaults
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
