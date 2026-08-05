#include "framework/test_utils.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace algorithm;

// Empty AlgorithmInput for tests that don't need real data.
static AlgorithmInput g_test_input;

// ─── Test IAlgorithm implementation (compact-only) ───

namespace {
struct TestForgeEngine : IForgeEngine {
    ForgeConfig _cfg;
    const ForgeConfig &get_config() const noexcept override { return _cfg; }
    void set_config(const ForgeConfig &c) noexcept override { _cfg = c; }
    int32_t forge_into(Item &, const Item &, const EnchReg &) const override { return 0; }
    std::pair<Item, int32_t> forge(const Item &t, const Item &s, const EnchReg &r) const override {
        Item c = t; return {std::move(c), forge_into(c, s, r)};
    }
    bool is_forgeable(const Item &, const Item &) const noexcept override { return true; }
};
} // namespace

class TestAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "test"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t) const noexcept override { return 0; }
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<TestForgeEngine>();
    }

    void execute(const AlgorithmInput &, ExecutionContext& ctx) override {
        for (int i = 0; i < 5; i++) {
            if (ctx.is_cancelled()) return;
            ctx.wait_if_paused();
            ctx.report_progress(static_cast<uint8_t>((i + 1) * 20), ProgressStatus::Exploring);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::vector<algorithm::EnchStep> solution;
        solution.push_back(algorithm::EnchStep{{}, {}, {}, 4});
        ctx.report_solution(solution);
    }
};

class SlowAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "slow"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t) const noexcept override { return 0; }
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<TestForgeEngine>();
    }

    void execute(const AlgorithmInput &, ExecutionContext& ctx) override {
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
    double evaluate(int16_t) const noexcept override { return 0; }
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<TestForgeEngine>();
    }

    void execute(const AlgorithmInput &, ExecutionContext&) override {
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

void test_reset_and_rerun() {
    // A terminal executor can be re-armed explicitly via reset() and run
    // again (the sandbox worker reuses one executor across MsgRun frames).
    auto algo = std::make_unique<TestAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);
    expect(executor.wait() == AlgorithmState::Completed, "first run completes");
    expect(executor.reset(), "reset() after Completed returns true");
    expect(executor.state() == AlgorithmState::Idle, "state back to Idle after reset");
    expect(!executor.reset(), "reset() while Idle is a no-op (false)");

    executor.start(g_test_input);
    expect(executor.wait() == AlgorithmState::Completed, "re-run completes");
    std::cout << "PASS: test_reset_and_rerun" << std::endl;
}

void test_reset_refuses_while_running() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    executor.start(g_test_input);
    // Poll until actually Running, then reset() must refuse.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (executor.state() != AlgorithmState::Running &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(executor.state() == AlgorithmState::Running, "should be Running");
    expect(executor.reset() == false, "reset() while Running refused");
    executor.wait();
    std::cout << "PASS: test_reset_refuses_while_running" << std::endl;
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

void test_cancel_before_start_noop() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    // cancel() before start() must not brick the executor — stays Idle
    executor.cancel();
    expect(executor.state() == AlgorithmState::Idle,
           "state should stay Idle after pre-start cancel()");

    executor.start(g_test_input);
    executor.wait();
    expect(executor.state() == AlgorithmState::Completed,
           "executor should still run after pre-start cancel()");
    std::cout << "PASS: test_cancel_before_start_noop" << std::endl;
}

void test_pause_resume_before_start_noop() {
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    // pause()/resume() before start() must be safe no-ops (no null deref)
    executor.pause();
    executor.resume();
    expect(executor.state() == AlgorithmState::Idle,
           "state should stay Idle after pre-start pause/resume");

    executor.start(g_test_input);
    executor.wait();
    expect(executor.state() == AlgorithmState::Completed,
           "executor should still run after pre-start pause/resume");
    std::cout << "PASS: test_pause_resume_before_start_noop" << std::endl;
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

// ─── dp_merge large-N + timeout cancellation ────────────────────────────
//
// The hard n caps were removed from dp_merge (n > 20) and bb_dp (n > 28): a
// large input must now be bounded by the Executor's max_search_time timeout
// rather than bailing to "no solution" instantly.

namespace {
AlgorithmInput make_direct_input(int n, uint8_t max_lvl, uint8_t level,
                                 std::chrono::milliseconds timeout) {
    std::vector<EnchInfo> infos(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        infos[static_cast<size_t>(i)].id         = static_cast<uint8_t>(i);
        infos[static_cast<size_t>(i)].mul        = 1;
        infos[static_cast<size_t>(i)].mul_b      = 1;
        infos[static_cast<size_t>(i)].max_lvl    = max_lvl;
        infos[static_cast<size_t>(i)].exc_mask   = 0;
        infos[static_cast<size_t>(i)].applicable = true;
    }

    Equipment eq;
    eq.id             = NSID("test");
    eq.max_durability = 1561;
    for (int i = 0; i < n; ++i)
        eq.applicable_enchs.insert(static_cast<uint8_t>(i));

    std::vector<NSID> global_ids;
    global_ids.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        global_ids.emplace_back(std::string("ench_") + std::to_string(i));

    AlgorithmInput input;
    input.config.forge.platform         = MCE::Java;
    input.config.mode                   = AlgorithmMode::direct;
    input.config.search.max_search_time = timeout;
    input.registry.init(std::move(infos), std::move(global_ids), eq);
    input.data = DirectPayload{};
    input.target.type = ItemType::Equip;
    for (int i = 0; i < n; ++i)
        input.target.enchs.insert(Ench{static_cast<uint8_t>(i), level});
    return input;
}

AlgorithmInput make_large_direct_input() {
    // 26 distinct level-5 enchants → 27 items (n > 20): the removed `n > 20`
    // bail would have returned "no solution" instantly; now the map-backed
    // search runs until the 200ms Executor timeout cancels it.  The
    // stoppable parallel_for makes the post-cancel drain prompt even at this
    // size (previously ~4 minutes of unwinding the 2^26 mask range).
    return make_direct_input(26, /*max_lvl=*/5, /*level=*/5,
                             std::chrono::milliseconds(200));
}

// A REACHABLE input that completes quickly, proving dp_merge still finds real
// solutions after the n-cap removal / ctx threading.  n is kept small: the
// uniform all-level-1 / all-multiplier-1 data (no exclusivity) defeats the
// Pareto pruning, so even n=15 takes ~3 minutes of exact DP.
AlgorithmInput make_reachable_large_input() {
    return make_direct_input(9, /*max_lvl=*/1, /*level=*/1,
                             std::chrono::milliseconds(0));  // unlimited
}
} // anonymous namespace

void test_dp_merge_large_n_timeout_cancel() {
    AlgorithmExecutor executor(std::make_unique<DPMergeAlgorithm>());
    executor.start(make_large_direct_input());

    auto final_state = executor.wait();
    auto out = executor.output();
    expect(final_state == AlgorithmState::Cancelled,
           "dp_merge with >20 items should be cancellable mid-search");
    expect(executor.output().solutions.empty(),
           "a cancelled dp_merge search should not produce a fake solution");
    std::cout << "PASS: test_dp_merge_large_n_timeout_cancel" << std::endl;
}

void test_dp_merge_large_n_completes() {
    AlgorithmExecutor executor(std::make_unique<DPMergeAlgorithm>());
    executor.start(make_reachable_large_input());

    auto final_state = executor.wait();
    auto out = executor.output();
    expect(final_state == AlgorithmState::Completed,
           "dp_merge with a reachable input should complete with a solution");
    expect(!out.solutions.empty(),
           "dp_merge large-N solution should be non-empty");
    std::cout << "PASS: test_dp_merge_large_n_completes" << std::endl;
}

void test_timeout_with_slow_algorithm() {
    // A cooperative algorithm (polls is_cancelled) must surface a timeout as
    // Cancelled.  Guards the Executor's timeout watcher: it must transition
    // the executor state to Cancelled, not just set the ctx flag.
    auto algo = std::make_unique<SlowAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));
    AlgorithmInput input = g_test_input;
    input.config.search.max_search_time = std::chrono::milliseconds(50);
    executor.start(std::move(input));
    auto final_state = executor.wait();
    expect(final_state == AlgorithmState::Cancelled,
           "executor timeout should surface as Cancelled for a cooperative algorithm");
    std::cout << "PASS: test_timeout_with_slow_algorithm" << std::endl;
}

int main() {
    try {
        test_constructor_null();
        test_initial_state();
        test_executor_lifecycle();
        test_double_start();
        test_reset_and_rerun();
        test_reset_refuses_while_running();
        test_executor_cancel();
        test_executor_pause_resume();
        test_cancel_before_start_noop();
        test_pause_resume_before_start_noop();
        test_executor_progress();
        test_output_not_valid_before_completion();
        test_output_has_steps_after_completion();
        test_serialization_stubs();
        test_executor_throwing_algorithm();
        test_timeout_with_slow_algorithm();
        test_dp_merge_large_n_completes();
        test_dp_merge_large_n_timeout_cancel();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
