#pragma once
#include "../BESQTypes.h"
#include "AlgorithmObserver.h"
#include "../utils/SPSCQueue.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

// ─── Observer event (for async queue) ───
struct ObserverEvent {
    enum Type { Progress, Solution, StateChange, Diagnostic, Completed };
    Type type;
    double progress_val{0};
    ProgressStatus status{ProgressStatus::Starting};
    EnchStepList steps;
    AlgorithmState prev_state{AlgorithmState::Idle};
    AlgorithmState curr_state{AlgorithmState::Idle};
};

// ─── Execution context (passed into IAlgorithm::execute) ───
// The algorithm pushes ObserverEvent into a lock-free queue.
// dispatch_events() drains the queue to attached observers.
class ExecutionContext {
public:
    ExecutionContext() = default;
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    // Cancel/pause
    void cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
    void pause() noexcept { _paused.store(true, std::memory_order_release); }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
    }
    bool is_cancelled() const noexcept { return _cancelled.load(std::memory_order_acquire); }
    bool is_paused() const noexcept  { return _paused.load(std::memory_order_acquire); }
    void wait_if_paused();

    // Event pushing (lock-free, with adaptive backpressure)
    // Latest progress is always readable via progress() — the event queue
    // uses adaptive sampling: high-frequency updates are coalesced.
    void report_progress(double percent, ProgressStatus status);
    void report_solution_found(const EnchStepList& solution);
    void report_state_change(AlgorithmState prev, AlgorithmState curr);

    // Event dispatch (drains queue to observers)
    void dispatch_events();

    // Observer management
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);
    bool has_observers() const noexcept {
        return _has_observers.load(std::memory_order_acquire);
    }

    // Result accumulation
    void append_output_steps(const EnchStepList& steps);
    std::vector<EnchStepList> get_accumulated_steps() const;

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

    std::atomic<bool> _has_observers{false};
    SPSCQueue<ObserverEvent, 256> _events;

    mutable std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _accum_mtx;
    std::vector<EnchStepList> _accumulated_steps;
    std::atomic<double> _progress{0.0};

    // ── Progress downsampling ──
    // Guards the SPSC push in report_progress. Only 1 in 64 calls
    // actually pushes an event (when (counter & 0x3F) == 1).
    // Non-atomic: only accessed from the single producer thread.
    uint32_t _progress_downsample{0};
};
