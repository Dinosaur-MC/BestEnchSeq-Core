#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/diagnostics/DiagnosticsService.h"
#include "domain/algorithm/diagnostics/DiagnosticsEvent.h"
#include <algorithm>
#include <mutex>

namespace algorithm {

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

void ExecutionContext::report_progress(uint8_t pct, ProgressStatus status) {
    _progress.store(pct, std::memory_order_release);

    if (pct == 100 || pct == 0) {
        _progress_pct.store(pct, std::memory_order_relaxed);
    } else {
        int8_t prev = _progress_pct.load(std::memory_order_relaxed);
        if (static_cast<int>(pct) - static_cast<int>(prev) < 5)
            return;
        if (!_progress_pct.compare_exchange_weak(prev, static_cast<int8_t>(pct),
                std::memory_order_relaxed, std::memory_order_relaxed))
            return;
    }

    DiagnosticsService::instance().push(
        DiagEventKind::Progress, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::ProgressPayload{pct, status});
}

namespace {
int32_t sum_step_costs(const std::vector<EnchStep>& steps) noexcept {
    int32_t total = 0;
    for (const auto& s : steps) total += s.cost;
    return total;
}
} // anonymous namespace

void ExecutionContext::report_solution(const std::vector<EnchStep>& steps) {
    auto sol = std::make_shared<const EnchSolution>(
        EnchSolution{steps, sum_step_costs(steps)});

    append_solution(sol);
    DiagnosticsService::instance().push(
        DiagEventKind::Solution, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::SolutionPayload{std::move(sol)});
}

void ExecutionContext::report_solution(std::vector<EnchStep>&& steps) {
    int32_t total_cost = sum_step_costs(steps);
    auto sol = std::make_shared<const EnchSolution>(
        EnchSolution{std::move(steps), total_cost});  // zero copy

    append_solution(sol);
    DiagnosticsService::instance().push(
        DiagEventKind::Solution, std::string{algorithm_name()}, task_id(),
        DiagnosticsEvent::SolutionPayload{std::move(sol)});
}

void ExecutionContext::append_solution(std::shared_ptr<const EnchSolution> solution) {
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

std::vector<EnchSolution> ExecutionContext::get_solutions() const {
    std::lock_guard lock(_sol_mtx);
    std::vector<EnchSolution> result;
    result.reserve(_solutions.size());
    for (const auto& sol : _solutions)
        result.push_back(*sol);  // single copy when output is actually requested
    return result;
}

} // namespace algorithm
