#pragma once
#include "../BESQTypes.h"
#include <algorithm> // IWYU pragma: export
#include <deque>
#include <iostream>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Forward declarations (full definitions in IAlgorithm.h, included in .cpp)
struct AlgorithmOutput;
struct AlgorithmInput;
class IAlgorithm;

// ─── Diagnostic info (placeholder for now) ───
struct DiagnosticInfo {
    std::string message;
    // Extended fields TBD in phase 2
};

// ─── Algorithm state machine ───
enum class AlgorithmState {
    Idle,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

// ─── Observer (streaming callbacks) ───
class AlgorithmObserver {
public:
    virtual ~AlgorithmObserver() = default;

    virtual void on_progress(double percent, std::string_view status) {}
    virtual void on_solution_found(const EnchStepList& solution) {}
    virtual void on_state_changed(AlgorithmState prev, AlgorithmState curr) {}
    virtual void on_diagnostic(const DiagnosticInfo& info) {}
    virtual void on_completed(const AlgorithmOutput& output) {}
};

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

    // --- Event pushing (called by algorithm, non-blocking) ---
    void report_progress(double percent, std::string_view status) {
        _progress.store(percent, std::memory_order_release);
        if (_has_observers.load(std::memory_order_acquire)) {
            std::lock_guard lock(_event_mtx);
            _events.push_back({ObserverEvent::Progress, percent, std::string(status), {}, {}});
        }
    }

    void report_solution_found(const EnchStepList& solution) {
        if (_has_observers.load(std::memory_order_acquire)) {
            std::lock_guard lock(_event_mtx);
            _events.push_back({ObserverEvent::Solution, 0, {}, solution, {}});
        }
        append_output_steps(solution);
    }

    void report_state_change(AlgorithmState prev, AlgorithmState curr) {
        if (_has_observers.load(std::memory_order_acquire)) {
            std::lock_guard lock(_event_mtx);
            _events.push_back({ObserverEvent::StateChange, 0, {}, {}, prev, curr});
        }
    }

    // --- Event dispatch (called by Executor after execution completes) ---
    // Flushes all queued events to attached observers.
    void dispatch_events() {
        if (!_has_observers.load(std::memory_order_acquire))
            return;

        std::deque<ObserverEvent> batch;
        std::vector<std::shared_ptr<AlgorithmObserver>> obs_snapshot;
        {
            std::lock_guard lock(_event_mtx);
            batch.swap(_events);
        }
        {
            std::lock_guard lock(_obs_mtx);
            obs_snapshot = _observers;
        }
        for (auto& e : batch) {
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

    mutable std::mutex _event_mtx;
    std::deque<ObserverEvent> _events;

    mutable std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _accum_mtx;
    std::vector<EnchStepList> _accumulated_steps;
    std::atomic<double> _progress{0.0};
};

// ─── AlgorithmExecutor (async execution engine) ───
class AlgorithmExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    // Non-copyable, non-movable
    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    // Lifecycle
    void start(const AlgorithmInput& input);
    void pause();
    void resume();
    void cancel();
    AlgorithmState wait();
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    // State queries
    AlgorithmState state() const noexcept;
    double progress() const noexcept;

    // Observer
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    // Result
    AlgorithmOutput output() const;

    // Serialization (phase 2 — stubs)
    std::vector<uint8_t> serialize_state() const { return {}; }
    bool restore_state(const std::vector<uint8_t>&) { return false; }

private:
    void _join_worker() noexcept;
    void _set_state(AlgorithmState new_state) noexcept;

    std::unique_ptr<IAlgorithm> _algorithm;
    std::unique_ptr<ExecutionContext> _ctx;
    std::optional<std::thread> _worker;
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    std::mutex _state_mtx;
    std::condition_variable _state_cv;
    std::chrono::steady_clock::time_point _start_time;
    std::chrono::milliseconds _computation_time{0};
};
