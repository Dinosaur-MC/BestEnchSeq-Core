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
    int pct = static_cast<int>(percent * 100.0);
    _progress.store(percent, std::memory_order_release);

    if (pct == 100 || pct == 0) {
        _progress_pct.store(pct, std::memory_order_relaxed);
        if (_sink.on_progress)
            _sink.on_progress(percent, status, _sink.context);
    } else {
        int prev = _progress_pct.load(std::memory_order_relaxed);
        if (pct - prev >= 5) {
            if (_progress_pct.compare_exchange_weak(prev, pct,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                if (_sink.on_progress)
                    _sink.on_progress(percent, status, _sink.context);
            }
        }
    }
}

void ExecutionContext::report_compact_solution(std::vector<compact::EnchStep> steps) {
    auto sol = std::make_shared<const compact::EnchSolution>(
        compact::EnchSolution{std::move(steps), 0});

    append_compact_solution(*sol);

    if (_sink.on_solution && _algo_name)
        _sink.on_solution(sol, _algo_name, _sink.context);
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
