#pragma once
#include "AlgorithmObserver.h"
#include "utils/SPSCQueue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

// ─── Hard limit on stored solutions ───
#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

// ─── Observer event (for async queue) ───
struct ObserverEvent {
    enum Type { Progress, Solution, StateChange, Diagnostic, Completed };
    Type type;
    double progress_val{0};
    ProgressStatus status{ProgressStatus::Starting};
    std::vector<compact::EnchStep> steps;
    AlgorithmState prev_state{AlgorithmState::Idle};
    AlgorithmState curr_state{AlgorithmState::Idle};
};

// ─── Execution context (passed into IAlgorithm::execute) ───
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
    void report_progress(double percent, ProgressStatus status);
    void report_compact_solution(std::vector<compact::EnchStep> solution);
    void report_state_change(AlgorithmState prev, AlgorithmState curr);

    // Event dispatch (drains queue to observers)
    void dispatch_events();

    // Observer management
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);
    bool has_observers() const noexcept {
        return _has_observers.load(std::memory_order_acquire);
    }

    // Result accumulation (compact steps only)
    void append_compact_steps(const std::vector<compact::EnchStep>& steps);
    std::vector<std::vector<compact::EnchStep>> get_accumulated_compact_steps() const;

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

    // ── Search config (hot-updatable at runtime) ──
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
    std::vector<std::pair<int32_t, std::vector<compact::EnchStep>>> _accumulated;
    std::atomic<double> _progress{0.0};

    // Progress downsampling
    uint32_t _progress_downsample{0};

    // Search config (shared_ptr atomic swap for lock-free reads)
    std::atomic<std::shared_ptr<const SearchConfig>> _search_config{nullptr};
};
