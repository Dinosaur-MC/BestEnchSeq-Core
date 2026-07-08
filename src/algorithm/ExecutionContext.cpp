#include "ExecutionContext.h"
#include "AlgorithmObserver.h"
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
    if (_has_observers.load(std::memory_order_acquire)) {
        ObserverEvent e;
        e.type = ObserverEvent::Solution;
        e.steps = solution;
        _events.push(std::move(e));
    }
    append_output_steps(solution);
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
    if (!_has_observers.load(std::memory_order_acquire))
        return;

    std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
    {
        std::lock_guard lock(_obs_mtx);
        obs_snapshot = _observers;
    }
    if (obs_snapshot.empty()) return;

    ObserverEvent e;
    while (_events.pop(e)) {
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
    std::lock_guard lock(_accum_mtx);
    _accumulated_steps.push_back(steps);
}

std::vector<EnchStepList> ExecutionContext::get_accumulated_steps() const {
    std::lock_guard lock(_accum_mtx);
    return _accumulated_steps;
}
