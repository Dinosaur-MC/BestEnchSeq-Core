#pragma once
#include "../BESQTypes.h"
#include <algorithm> // IWYU pragma: export
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

// Forward declaration (full definition in Task 4)
struct AlgorithmOutput;

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

// ─── Execution context (passed into IAlgorithm::execute) ───
class ExecutionContext {
public:
    ExecutionContext() = default;

    // Non-copyable, non-movable
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
        // Spin briefly; block on CV if still paused
        std::unique_lock lock(_pause_mtx);
        _pause_cv.wait(lock, [this] {
            return !_paused.load(std::memory_order_acquire) ||
                    _cancelled.load(std::memory_order_acquire);
        });
    }

    // --- Progress reporting ---
    void report_progress(double percent, std::string_view status) {
        _progress.store(percent, std::memory_order_release);
        auto guard = shared_lock_observers();
        for (auto& obs : _observers)
            obs->on_progress(percent, status);
    }

    void report_solution_found(const EnchStepList& solution) {
        auto guard = shared_lock_observers();
        for (auto& obs : _observers)
            obs->on_solution_found(solution);
        append_output_steps(solution);
    }

    void report_diagnostic(const DiagnosticInfo& info) {
        auto guard = shared_lock_observers();
        for (auto& obs : _observers)
            obs->on_diagnostic(info);
    }

    void report_state_change(AlgorithmState prev, AlgorithmState curr) {
        auto guard = shared_lock_observers();
        for (auto& obs : _observers)
            obs->on_state_changed(prev, curr);
    }

    void report_completed(const AlgorithmOutput& output) {
        auto guard = shared_lock_observers();
        for (auto& obs : _observers)
            obs->on_completed(output);
    }

    // --- Observer management ---
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer) {
        std::unique_lock lock(_observer_mtx);
        _observers.push_back(std::move(observer));
    }

    void detach_observer(std::shared_ptr<AlgorithmObserver> observer) {
        std::unique_lock lock(_observer_mtx);
        auto it = std::find(_observers.begin(), _observers.end(), observer);
        if (it != _observers.end())
            _observers.erase(it);
    }

    bool has_observers() const noexcept {
        std::shared_lock lock(_observer_mtx);
        return !_observers.empty();
    }

    // --- Result accumulation ---
    void append_output_steps(const EnchStepList& steps) {
        std::unique_lock lock(_output_mtx);
        _accumulated_steps.push_back(steps);
    }

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

private:
    std::shared_lock<std::shared_mutex> shared_lock_observers() const {
        return std::shared_lock(_observer_mtx);
    }

    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

    mutable std::shared_mutex _observer_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _output_mtx;
    std::vector<EnchStepList> _accumulated_steps;
    std::atomic<double> _progress{0.0};
};
