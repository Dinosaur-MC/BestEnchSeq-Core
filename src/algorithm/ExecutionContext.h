#pragma once
#include "../BESQTypes.h"
#include "AlgorithmObserver.h"
#include "../utils/SPSCQueue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

// ─── Hard limit on stored solutions ───
// Any algorithm holding or outputting solutions MUST cap at this value.
// When exceeded, only the cheapest solutions are retained.
#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

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

    // ── Search config (hot-updatable at runtime) ──
    // These parameters can be changed while the algorithm is running/paused.
    // The algorithm reads the latest values at its next checkpoint.
    struct SearchConfig {
        int32_t max_solutions = 0;   // 0 = unlimited
        int32_t max_depth = 0;       // 0 = unlimited
        std::chrono::milliseconds max_search_time{0}; // 0 = unlimited
    };

    SearchConfig get_search_config() const {
        auto ptr = _search_config.load(std::memory_order_acquire);
        return ptr ? *ptr : SearchConfig{};
    }

    void set_search_config(SearchConfig cfg) {
        _search_config.store(
            std::make_shared<const SearchConfig>(std::move(cfg)),
            std::memory_order_release);
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
    // Stored as (total_cost, steps) pairs, sorted by cost ascending.
    // Only the top BESQ_MAX_SOLUTIONS are retained.
    std::vector<std::pair<int32_t, EnchStepList>> _accumulated;
    std::atomic<double> _progress{0.0};

    // ── Progress downsampling ──
    // Guards the SPSC push in report_progress. Only 1 in 64 calls
    // actually pushes an event (when (counter & 0x3F) == 1).
    // Non-atomic: only accessed from the single producer thread.
    uint32_t _progress_downsample{0};

    // ── Search config (read by algorithm, written by external thread) ──
    // Uses shared_ptr atomic swap for lock-free reads on the algorithm side.
    std::atomic<std::shared_ptr<const SearchConfig>> _search_config{nullptr};
};
