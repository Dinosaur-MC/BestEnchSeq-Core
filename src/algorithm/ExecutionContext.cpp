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

void ExecutionContext::report_solution(const std::vector<compact::EnchStep>& steps) {
    // Compute cost
    int32_t total_cost = 0;
    for (const auto& s : steps)
        total_cost += s.cost;

    // Copy steps into shared_ptr — the single owner (one copy, unavoidable).
    auto sol = std::make_shared<const compact::EnchSolution>(
        compact::EnchSolution{steps, total_cost});

    // Store in _solutions (shared_ptr RC+1, no data copy)
    append_solution(sol);

    // Push to observer (shared_ptr RC+1, no data copy)
    DiagnosticsService::instance().push(
        DiagEventKind::Solution, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::SolutionPayload{std::move(sol)});
}

void ExecutionContext::append_solution(std::shared_ptr<const compact::EnchSolution> solution) {
    auto cost = solution->total_cost;
    std::lock_guard lock(_sol_mtx);

    auto it = std::upper_bound(_solutions.begin(), _solutions.end(),
                                cost,
                                [](int32_t c, const auto& sol) { return c < sol->total_cost; });

    if (_solutions.size() >= BESQ_MAX_SOLUTIONS) [[likely]] {
        if (it == _solutions.end())
            return;
        _solutions.emplace(it, std::move(solution));
        _solutions.resize(BESQ_MAX_SOLUTIONS);
    } else {
        _solutions.emplace(it, std::move(solution));
    }
}

std::vector<compact::EnchSolution> ExecutionContext::get_solutions() const {
    std::lock_guard lock(_sol_mtx);
    std::vector<compact::EnchSolution> result;
    result.reserve(_solutions.size());
    for (const auto& sol : _solutions)
        result.push_back(*sol);  // single copy when output is actually requested
    return result;
}
