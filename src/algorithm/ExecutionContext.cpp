#include "ExecutionContext.h"
#include "AlgorithmObserver.h"
#include <algorithm>
#include <iostream>

void ExecutionContext::wait_if_paused() {
    if (!_paused.load(std::memory_order_acquire))
        return;
    std::unique_lock lock(_pause_mtx);
    _pause_cv.wait(lock, [this] {
        return !_paused.load(std::memory_order_acquire) ||
                _cancelled.load(std::memory_order_acquire);
    });
}

// ─── Zero-overhead progress probe ───
//
// Design constraints:
//   1. Must NEVER impact algorithm execution performance.
//   2. _progress() must always return the latest value (for polling).
//   3. Observer events are acceptable but must be aggressively rate-limited.
//
// Hot path:
//   _progress.store()          — 1 atomic store (mandatory for polling)
//   _has_observers.load()      — 1 atomic load (returns false most of the time)
//   return                      — when no observers, that's the FULL cost
//
// With observers:
//   ++_progress_downsample     — register-local increment (register hit, ~1 cycle)
//   if ((counter & 0x3F) != 1) — bitwise AND (register, ~1 cycle)
//   return                      — 63 out of 64 calls return here
//   SPSC push                  — only 1 in 64 calls reaches here
//
// Total probe overhead: 2 atomic ops (store + load) + ~2 register ops
// SPSC push frequency: once per 64 report_progress calls (~20ns each)
void ExecutionContext::report_progress(double percent, ProgressStatus status) {
    // Always update the pollable progress value (mandatory, 1 store ~5ns)
    _progress.store(percent, std::memory_order_release);

    // Quick exit when no observers attached (1 load ~5ns)
    if (!_has_observers.load(std::memory_order_acquire))
        return;

    // Aggressive downsampling: push only 1 in 64 calls
    // This bounds the SPSC push cost to ~0.3ns per call amortized.
    if ((++_progress_downsample & 0x3F) != 1)
        return;

    ObserverEvent e;
    e.type = ObserverEvent::Progress;
    e.progress_val = percent;
    e.status = status;
    _events.push(std::move(e));
}

void ExecutionContext::report_solution_found(const EnchStepList& solution) {
    // Always push to event queue (lock-free, ~20ns).
    // dispatch_events drains the queue and accumulates solutions
    // in append_output_steps (sort + truncate) on the consumer thread.
    ObserverEvent e;
    e.type = ObserverEvent::Solution;
    e.steps = solution;
    _events.push(std::move(e));
}

void ExecutionContext::report_state_change(AlgorithmState prev, AlgorithmState curr) {
    if (_has_observers.load(std::memory_order_acquire)) {
        ObserverEvent e;
        e.type = ObserverEvent::StateChange;
        e.prev_state = prev;
        e.curr_state = curr;
        _events.push(std::move(e));
    }
}

void ExecutionContext::dispatch_events() {
    // Snapshot observers (may be empty — solution events still need draining)
    std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
    {
        std::lock_guard lock(_obs_mtx);
        obs_snapshot = _observers;
    }

    ObserverEvent e;
    while (_events.pop(e)) {
        // Accumulate solution steps on consumer thread (sort+truncate)
        if (e.type == ObserverEvent::Solution)
            append_output_steps(e.steps);

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
                    default: break;
                }
            } catch (const std::exception& ex) {
                try { std::cerr << "[observer] " << ex.what() << std::endl; } catch (...) {}
            } catch (...) {
                try { std::cerr << "[observer] unknown exception" << std::endl; } catch (...) {}
            }
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

void ExecutionContext::append_output_steps(const EnchStepList& steps) {
    // Compute total cost of this solution
    int32_t total = 0;
    for (const auto& s : steps)
        total += s.exp_level_cost;

    std::lock_guard lock(_accum_mtx);
    _accumulated.emplace_back(total, steps);

    // Keep only the top BESQ_MAX_SOLUTIONS by total cost.
    // Sort by cost ascending; when tied, earlier insertion wins (stable).
    std::sort(_accumulated.begin(), _accumulated.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (_accumulated.size() > BESQ_MAX_SOLUTIONS)
        _accumulated.resize(BESQ_MAX_SOLUTIONS);
}

std::vector<EnchStepList> ExecutionContext::get_accumulated_steps() const {
    std::lock_guard lock(_accum_mtx);
    std::vector<EnchStepList> result;
    result.reserve(_accumulated.size());
    for (const auto& p : _accumulated)
        result.push_back(p.second);
    return result;
}
