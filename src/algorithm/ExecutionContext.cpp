#include "ExecutionContext.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/diagnostics/DiagnosticsEvent.h"
#include <algorithm>
#include <mutex>

ExecutionContext::ExecutionContext(size_t task_id, const char* algorithm_name) noexcept
    : _task_id(task_id), _algo_name(algorithm_name) {}

void ExecutionContext::wait_if_paused() {
    if (!_paused.load(std::memory_order_acquire))
        return;
    std::unique_lock lock(_pause_mtx);
    _pause_cv.wait(lock, [this] {
        return !_paused.load(std::memory_order_acquire) ||
                _cancelled.load(std::memory_order_acquire);
    });
}

void ExecutionContext::report_progress(double percent, ProgressStatus status) {
    int pct = static_cast<int>(percent * 100.0);
    _progress.store(percent, std::memory_order_release);

    if (pct == 100 || pct == 0) {
        _progress_pct.store(pct, std::memory_order_relaxed);
    } else {
        int prev = _progress_pct.load(std::memory_order_relaxed);
        if (pct - prev < 5)
            return;
        if (!_progress_pct.compare_exchange_weak(prev, pct,
                std::memory_order_relaxed, std::memory_order_relaxed))
            return;
    }

    DiagnosticsService::instance().push(
        DiagEventKind::Progress, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::ProgressPayload{percent, status});
}

void ExecutionContext::report_solution(std::vector<compact::EnchStep> steps) {
    auto sol = std::make_shared<const compact::EnchSolution>(
        compact::EnchSolution{std::move(steps), 0});

    append_solution(*sol);

    DiagnosticsService::instance().push(
        DiagEventKind::Solution, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::SolutionPayload{std::move(sol)});
}

void ExecutionContext::append_solution(compact::EnchSolution solution) {
    if (solution.total_cost == 0) {
        for (const auto& s : solution.steps)
            solution.total_cost += s.cost;
    }

    std::lock_guard lock(_sol_mtx);

    // _solutions is always sorted — find the insertion point via binary
    // search then insert directly, avoiding a full O(n log n) sort.
    // O(n) total per call (O(log n) search + O(n) shift).
    auto it = std::upper_bound(_solutions.begin(), _solutions.end(),
                                solution.total_cost,
                                [](int32_t cost, const auto& sol) { return cost < sol.total_cost; });

    if (_solutions.size() >= BESQ_MAX_SOLUTIONS) [[likely]] {
        if (it == _solutions.end())
            return;                     // worse than all kept solutions — discard
        _solutions.emplace(it, std::move(solution));
        _solutions.resize(BESQ_MAX_SOLUTIONS);
    } else {
        _solutions.emplace(it, std::move(solution));
    }
}

std::vector<compact::EnchSolution> ExecutionContext::get_solutions() const {
    std::lock_guard lock(_sol_mtx);
    return _solutions;
}
