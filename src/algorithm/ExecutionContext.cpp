#include "ExecutionContext.h"
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

void ExecutionContext::report_progress(double percent, std::string_view status) {
    _progress.store(percent, std::memory_order_release);
    if (_has_observers.load(std::memory_order_acquire))
        _events.push({ObserverEvent::Progress, percent, std::string(status), {}, {}});
}

void ExecutionContext::report_solution_found(const EnchStepList& solution) {
    if (_has_observers.load(std::memory_order_acquire))
        _events.push({ObserverEvent::Solution, 0, {}, solution, {}});
    append_output_steps(solution);
}

void ExecutionContext::report_state_change(AlgorithmState prev, AlgorithmState curr) {
    if (_has_observers.load(std::memory_order_acquire))
        _events.push({ObserverEvent::StateChange, 0, {}, {}, prev, curr});
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

    auto cursor = _events.read_cursor();
    ObserverEvent e;
    while (_events.read(cursor, e)) {
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
