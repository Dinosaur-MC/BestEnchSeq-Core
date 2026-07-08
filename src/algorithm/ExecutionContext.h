#pragma once
#include "../BESQTypes.h"
#include "AlgorithmObserver.h"
#include "../utils/SPMCQueue.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// ─── Observer event (for async queue) ───
struct ObserverEvent {
    enum Type { Progress, Solution, StateChange, Diagnostic, Completed };
    Type type;
    double progress_val{0};
    std::string status;
    EnchStepList steps;
    AlgorithmState prev_state{AlgorithmState::Idle};
    AlgorithmState curr_state{AlgorithmState::Idle};
};

// ─── Execution context (passed into IAlgorithm::execute) ───
// The algorithm pushes ObserverEvent into a queue (non-blocking).
// External code calls dispatch_events() to flush events to observers.
// This decouples algorithm execution from observer processing.
class ExecutionContext {
public:
    ExecutionContext() = default;

    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    // --- Cancel/pause (called by Executor thread) ---
    void cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
    void pause() noexcept {
        _paused.store(true, std::memory_order_release);
    }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
    }

    // --- Checked by algorithm during execution ---
    bool is_cancelled() const noexcept {
        return _cancelled.load(std::memory_order_acquire);
    }
    bool is_paused() const noexcept {
        return _paused.load(std::memory_order_acquire);
    }
    void wait_if_paused() {
        if (!_paused.load(std::memory_order_acquire))
            return;
        std::unique_lock lock(_pause_mtx);
        _pause_cv.wait(lock, [this] {
            return !_paused.load(std::memory_order_acquire) ||
                    _cancelled.load(std::memory_order_acquire);
        });
    }

    // --- Event pushing (called by algorithm, lock-free SPMCQueue) ---
    void report_progress(double percent, std::string_view status) {
        _progress.store(percent, std::memory_order_release);
        if (_has_observers.load(std::memory_order_acquire))
            _events.push({ObserverEvent::Progress, percent, std::string(status), {}, {}});
    }

    void report_solution_found(const EnchStepList& solution) {
        if (_has_observers.load(std::memory_order_acquire))
            _events.push({ObserverEvent::Solution, 0, {}, solution, {}});
        append_output_steps(solution);
    }

    void report_state_change(AlgorithmState prev, AlgorithmState curr) {
        if (_has_observers.load(std::memory_order_acquire))
            _events.push({ObserverEvent::StateChange, 0, {}, {}, prev, curr});
    }

    // --- Event dispatch (called by Executor after execution completes) ---
    // Drains the lock-free SPMCQueue and distributes events to observers.
    void dispatch_events() {
        if (!_has_observers.load(std::memory_order_acquire))
            return;

        std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
        {
            std::lock_guard lock(_obs_mtx);
            obs_snapshot = _observers;
        }
        if (obs_snapshot.empty()) return;

        auto cursor = _events.read_cursor();
        while (const auto* e = _events.read(cursor)) {
            for (auto& obs : obs_snapshot) {
                try {
                    switch (e->type) {
                        case ObserverEvent::Progress:
                            obs->on_progress(e->progress_val, e->status); break;
                        case ObserverEvent::Solution:
                            obs->on_solution_found(e->steps); break;
                        case ObserverEvent::StateChange:
                            obs->on_state_changed(e->prev_state, e->curr_state); break;
                        default: break;
                    }
                } catch (const std::exception& ex) {
                    // Observer exception must never propagate to algorithm
                    try { std::cerr << "[observer] " << ex.what() << std::endl; } catch (...) {}
                } catch (...) {
                    try { std::cerr << "[observer] unknown exception" << std::endl; } catch (...) {}
                }
            }
        }
    }

    // --- Observer management ---
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer) {
        std::lock_guard lock(_obs_mtx);
        _observers.push_back(std::move(observer));
        _has_observers.store(true, std::memory_order_release);
    }

    void detach_observer(std::shared_ptr<AlgorithmObserver> observer) {
        std::lock_guard lock(_obs_mtx);
        auto it = std::find(_observers.begin(), _observers.end(), observer);
        if (it != _observers.end())
            _observers.erase(it);
        _has_observers.store(!_observers.empty(), std::memory_order_release);
    }

    bool has_observers() const noexcept {
        return _has_observers.load(std::memory_order_acquire);
    }

    // --- Result accumulation ---
    void append_output_steps(const EnchStepList& steps) {
        std::lock_guard lock(_accum_mtx);
        _accumulated_steps.push_back(steps);
    }

    std::vector<EnchStepList> get_accumulated_steps() const {
        std::lock_guard lock(_accum_mtx);
        return _accumulated_steps;
    }

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

    std::atomic<bool> _has_observers{false};

    // Lock-free event queue: algorithm pushes, dispatch_events drains
    SPMCQueue<ObserverEvent, 256> _events;

    mutable std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _accum_mtx;
    std::vector<EnchStepList> _accumulated_steps;
    std::atomic<double> _progress{0.0};
};
