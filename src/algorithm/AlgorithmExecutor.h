#pragma once
#include "../BESQTypes.h"
#include <algorithm> // IWYU pragma: export
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
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

    std::vector<EnchStepList> get_accumulated_steps() const {
        std::unique_lock lock(_output_mtx);
        return _accumulated_steps;
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
};
