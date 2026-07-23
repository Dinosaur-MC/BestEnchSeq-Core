#include "framework/test_utils.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"

using namespace algorithm;

#include <chrono>
#include <thread>
#include <atomic>

void test_cancel() {
    algorithm::ExecutionContext ctx(1, "test");
    expect(!ctx.is_cancelled(), "initially not cancelled");
    ctx.cancel();
    expect(ctx.is_cancelled(), "cancelled after cancel()");
    std::cout << "PASS: test_cancel" << std::endl;
}

void test_pause_resume() {
    algorithm::ExecutionContext ctx(1, "test");
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
    algorithm::ExecutionContext ctx(1, "test");
    ctx.report_progress(50, ProgressStatus::Exploring);
    expect(ctx.progress() == 0.5, "progress should be 0.5");
    std::cout << "PASS: test_progress" << std::endl;
}

void test_diagnostic_log() {
    algorithm::ExecutionContext ctx(1, "test");

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
    algorithm::ExecutionContext ctx(1, "test");

    std::vector<algorithm::EnchStep> steps;
    steps.push_back(algorithm::EnchStep{{}, {}, 5});
    steps.push_back(algorithm::EnchStep{{}, {}, 3});
    ctx.report_solution(steps);

    auto solutions = ctx.get_solutions();
    expect(solutions.size() == 1, "should accumulate one solution");
    expect(solutions[0].total_cost == 8, "total cost should be 8");
    std::cout << "PASS: test_solution_accumulation" << std::endl;
}

void test_wait_if_paused_resume() {
    algorithm::ExecutionContext ctx(1, "test");
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

// ─── Mock observer + observer dispatch tests ─────────────────────────────────

#include "domain/algorithm/diagnostics/DiagnosticsService.h"

struct MockObserver : AlgorithmObserver {
    size_t seen_task{0};
    int progress_cnt{0};
    int solution_cnt{0};
    int state_cnt{0};
    int diagnostic_cnt{0};
    int completed_cnt{0};
    uint8_t last_pct{0};
    AlgorithmState last_prev{};
    AlgorithmState last_curr{};
    AlgorithmOutput last_output{};
    std::optional<size_t> accept_only;

    bool accept_task_id(size_t t) const override {
        return !accept_only.has_value() || t == *accept_only;
    }
    void on_progress(size_t task_id, uint8_t pct, ProgressStatus) override {
        ++progress_cnt; seen_task = task_id; last_pct = pct;
    }
    void on_solution_found(size_t task_id, const std::vector<algorithm::EnchStep>&) override {
        ++solution_cnt; seen_task = task_id;
    }
    void on_state_changed(size_t task_id, AlgorithmState p, AlgorithmState c) override {
        ++state_cnt; seen_task = task_id; last_prev = p; last_curr = c;
    }
    void on_diagnostic(size_t task_id, const DiagnosticInfo&) override {
        ++diagnostic_cnt; seen_task = task_id;
    }
    void on_completed(size_t task_id, const AlgorithmOutput& o) override {
        ++completed_cnt; seen_task = task_id; last_output = o;
    }
};

static void drain_events() { DiagnosticsService::instance().flush(); }

void test_observer_progress() {
    DiagnosticsService::instance().set_persist(false);
    auto obs = std::make_shared<MockObserver>();
    DiagnosticsService::instance().attach_observer(obs);

    algorithm::ExecutionContext ctx(42, "test_obs");
    ctx.report_progress(50, ProgressStatus::Exploring);
    drain_events();

    expect(obs->progress_cnt == 1, "on_progress called once");
    expect(obs->seen_task == 42, "task_id matches");
    expect(obs->last_pct == 50, "pct is 50");
    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_progress" << std::endl;
}

void test_observer_solution() {
    auto obs = std::make_shared<MockObserver>();
    DiagnosticsService::instance().attach_observer(obs);

    algorithm::ExecutionContext ctx(7, "test_sol");
    algorithm::EnchStep dummy{};
    std::vector<algorithm::EnchStep> steps;
    steps.push_back(dummy);
    ctx.report_solution(steps);
    drain_events();

    expect(obs->solution_cnt == 1, "on_solution_found called once");
    expect(obs->seen_task == 7, "task_id matches");
    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_solution" << std::endl;
}

void test_observer_solution_rvalue() {
    auto obs = std::make_shared<MockObserver>();
    DiagnosticsService::instance().attach_observer(obs);

    algorithm::ExecutionContext ctx(8, "test_sol_rv");
    std::vector<algorithm::EnchStep> steps;
    steps.push_back({{}, {}, 3});
    steps.push_back({{}, {}, 7});
    ctx.report_solution(std::move(steps));
    drain_events();

    expect(obs->solution_cnt == 1, "on_solution_found called once");
    expect(obs->seen_task == 8, "task_id matches");
    auto sols = ctx.get_solutions();
    expect(sols.size() == 1, "one solution accumulated");
    expect(sols[0].total_cost == 10, "total_cost correct");
    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_solution_rvalue" << std::endl;
}

void test_observer_state_change() {
    auto obs = std::make_shared<MockObserver>();
    DiagnosticsService::instance().attach_observer(obs);

    DiagnosticsService::instance().push(
        DiagEventKind::StateChange, "test", 99,
        DiagnosticsEvent::StatePayload{AlgorithmState::Running, AlgorithmState::Completed});
    drain_events();

    expect(obs->state_cnt == 1, "on_state_changed called once");
    expect(obs->seen_task == 99, "task_id matches");
    expect(obs->last_prev == AlgorithmState::Running, "prev is Running");
    expect(obs->last_curr == AlgorithmState::Completed, "curr is Completed");
    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_state_change" << std::endl;
}

void test_observer_exit() {
    DiagnosticsService::instance().set_persist(false);
    auto obs = std::make_shared<MockObserver>();
    DiagnosticsService::instance().attach_observer(obs);

    algorithm::ExecutionContext ctx(33, "test_exit");
    auto diag = std::make_unique<AlgorithmDiagnostics>();
    diag->status = "Complete";
    diag->solution_cost = 99;
    ctx.set_exit_diagnostics(std::move(diag));

    AlgorithmOutput out;
    out.algorithm_name = "test_exit";
    out.task_id = 33;
    DiagnosticsService::instance().push(
        DiagEventKind::Exit, "test_exit", 33,
        DiagnosticsEvent::ExitPayload{
            ctx.consume_exit_diagnostics(),
            std::move(out),
            "Complete",
            100,
            {"nodes_visited", 1000},
            {"nodes_pruned",  500},
            {"steps_forged",  200},
        });
    drain_events();

    expect(obs->diagnostic_cnt == 1, "on_diagnostic called once");
    expect(obs->completed_cnt == 1, "on_completed called once");
    expect(obs->seen_task == 33, "task_id matches");
    expect(obs->last_output.task_id == 33, "output task_id matches");
    expect(obs->last_output.algorithm_name == "test_exit", "algo name matches");
    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_exit" << std::endl;
}

void test_observer_task_id_filter() {
    auto obs = std::make_shared<MockObserver>();
    obs->accept_only = 42;
    DiagnosticsService::instance().attach_observer(obs);

    DiagnosticsService::instance().push(
        DiagEventKind::StateChange, "test", 42,
        DiagnosticsEvent::StatePayload{AlgorithmState::Idle, AlgorithmState::Running});
    drain_events();
    expect(obs->state_cnt == 1, "accepted matching task_id");

    DiagnosticsService::instance().push(
        DiagEventKind::StateChange, "test", 99,
        DiagnosticsEvent::StatePayload{AlgorithmState::Running, AlgorithmState::Completed});
    drain_events();
    expect(obs->state_cnt == 1, "filtered non-matching task_id (count unchanged)");

    DiagnosticsService::instance().detach_observer(obs);
    std::cout << "PASS: test_observer_task_id_filter" << std::endl;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    try {
        test_cancel();
        test_pause_resume();
        test_progress();
        test_diagnostic_log();
        test_solution_accumulation();
        test_wait_if_paused_resume();

        test_observer_progress();
        test_observer_solution();
        test_observer_solution_rvalue();
        test_observer_state_change();
        test_observer_exit();
        test_observer_task_id_filter();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
