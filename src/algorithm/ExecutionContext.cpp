#include "ExecutionContext.h"
#include <algorithm>
#include <mutex>

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
    (void)status;
    _progress.store(percent, std::memory_order_release);
}

void ExecutionContext::report_compact_solution(std::vector<compact::EnchStep> solution) {
    append_compact_solution({std::move(solution), 0});
}

void ExecutionContext::report_diagnostic(std::string_view key, int64_t value) {
    _diagnostic_log.emplace_back(key.data(), value);   // store raw int64_t + const char* key
}

void ExecutionContext::report_diagnostic(std::string_view key, std::string value) {
    _diagnostic_log.emplace_back(key.data(), std::move(value));
}

std::vector<std::pair<const char*, DiagnosticValue>> ExecutionContext::consume_diagnostic_log() {
    auto result = std::move(_diagnostic_log);
    _diagnostic_log.clear();
    return result;
}

void ExecutionContext::append_compact_solution(compact::EnchSolution solution) {
    if (solution.total_cost == 0) {
        for (const auto& s : solution.steps)
            solution.total_cost += s.cost;
    }

    std::lock_guard lock(_accum_mtx);

    // _accumulated is always sorted — find the insertion point via binary
    // search then insert directly, avoiding a full O(n log n) sort.
    // O(n) total per call (O(log n) search + O(n) shift).
    auto it = std::upper_bound(_accumulated.begin(), _accumulated.end(),
                                solution.total_cost,
                                [](int32_t cost, const auto& sol) { return cost < sol.total_cost; });

    if (_accumulated.size() >= BESQ_MAX_SOLUTIONS) [[likely]] {
        if (it == _accumulated.end())
            return;                     // worse than all kept solutions — discard
        _accumulated.emplace(it, std::move(solution));
        _accumulated.resize(BESQ_MAX_SOLUTIONS);
    } else {
        _accumulated.emplace(it, std::move(solution));
    }
}

std::vector<compact::EnchSolution> ExecutionContext::get_solutions() const {
    std::lock_guard lock(_accum_mtx);
    return _accumulated;
}
