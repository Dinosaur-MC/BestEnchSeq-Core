#include "ExecutionContext.h"
#include "AlgorithmObserver.h"
#include "log/log.hpp"
#include <algorithm>

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
    _progress.store(percent, std::memory_order_release);

    if (!_has_observers.load(std::memory_order_acquire))
        return;

    if ((++_progress_downsample & 0x3F) != 1)
        return;

    ObserverEvent e;
    e.type = ObserverEvent::Progress;
    e.progress_val = percent;
    e.status = status;
    _events.try_push(std::move(e));
}

void ExecutionContext::report_compact_solution(std::vector<compact::EnchStep> solution) {
    ObserverEvent e;
    e.type = ObserverEvent::Solution;
    e.steps = std::move(solution);
    _events.try_push(std::move(e));
}

void ExecutionContext::report_state_change(AlgorithmState prev, AlgorithmState curr) {
    if (_has_observers.load(std::memory_order_acquire)) {
        ObserverEvent e;
        e.type = ObserverEvent::StateChange;
        e.prev_state = prev;
        e.curr_state = curr;
        _events.try_push(std::move(e));
    }
}

void ExecutionContext::dispatch_events() {
    std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
    {
        std::lock_guard lock(_obs_mtx);
        obs_snapshot = _observers;
    }

    ObserverEvent e;
    while (_events.try_pop(e)) {
        if (e.type == ObserverEvent::Solution)
            append_compact_steps(e.steps);

        if (obs_snapshot.empty()) continue;
        for (auto& obs : obs_snapshot) {
            try {
                switch (e.type) {
                    case ObserverEvent::Progress:
                        obs->on_progress(e.progress_val, e.status); break;
                    case ObserverEvent::Solution:
                        obs->on_solution_found(e.steps); break;
                    case ObserverEvent::StateChange:
                        obs->on_state_changed(e.prev_state, e.curr_state); break;
                    case ObserverEvent::Diagnostic:
                        obs->on_diagnostic(DiagnosticInfo{e.diagnostic_msg}); break;
                    case ObserverEvent::Completed:
                        break; // handled via notify_completed()
                }
            } catch (const std::exception& ex) {
                try { LOG_ERROR("[observer] %s", ex.what()); } catch (...) {}
            } catch (...) {
                try { LOG_ERROR("[observer] unknown exception"); } catch (...) {}
            }
        }
    }
}

void ExecutionContext::notify_completed(const AlgorithmOutput& output) {
    std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
    {
        std::lock_guard lock(_obs_mtx);
        obs_snapshot = _observers;
    }
    for (auto& obs : obs_snapshot) {
        try {
            obs->on_completed(output);
        } catch (const std::exception& ex) {
            try { LOG_ERROR("[observer] %s", ex.what()); } catch (...) {}
        } catch (...) {
            try { LOG_ERROR("[observer] unknown exception"); } catch (...) {}
        }
    }
}

void ExecutionContext::attach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    _observers.push_back(std::move(observer));
    _has_observers.store(true, std::memory_order_release);
}

void ExecutionContext::detach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    auto it = std::find(_observers.begin(), _observers.end(), observer);
    if (it != _observers.end())
        _observers.erase(it);
    _has_observers.store(!_observers.empty(), std::memory_order_release);
}

void ExecutionContext::append_compact_steps(const std::vector<compact::EnchStep>& steps) {
    int32_t total = 0;
    for (const auto& s : steps)
        total += s.cost;

    std::lock_guard lock(_accum_mtx);
    _accumulated.emplace_back(total, steps);

    std::sort(_accumulated.begin(), _accumulated.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (_accumulated.size() > BESQ_MAX_SOLUTIONS)
        _accumulated.resize(BESQ_MAX_SOLUTIONS);
}

std::vector<std::vector<compact::EnchStep>> ExecutionContext::get_accumulated_compact_steps() const {
    std::lock_guard lock(_accum_mtx);
    std::vector<std::vector<compact::EnchStep>> result;
    result.reserve(_accumulated.size());
    for (const auto& p : _accumulated)
        result.push_back(p.second);
    return result;
}
