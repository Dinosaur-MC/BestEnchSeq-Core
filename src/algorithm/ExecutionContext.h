#pragma once
#include "algorithm/diagnostics/AlgorithmObserver.h"
#include "utils/queue/SPSCQueue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

struct ObserverEvent {
    enum Type { Progress, Solution, StateChange, Diagnostic, Completed };
    Type type;
    double progress_val{0};
    ProgressStatus status{ProgressStatus::Starting};
    std::vector<compact::EnchStep> steps;
    AlgorithmState prev_state{AlgorithmState::Idle};
    AlgorithmState curr_state{AlgorithmState::Idle};
    std::string diagnostic_msg;
};

class ExecutionContext {
public:
    ExecutionContext() = default;
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    void cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
    void pause() noexcept { _paused.store(true, std::memory_order_release); }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
    }
    bool is_cancelled() const noexcept { return _cancelled.load(std::memory_order_acquire); }
    bool is_paused() const noexcept  { return _paused.load(std::memory_order_acquire); }
    void wait_if_paused();

    void report_progress(double percent, ProgressStatus status);
    void report_compact_solution(std::vector<compact::EnchStep> solution);
    void report_state_change(AlgorithmState prev, AlgorithmState curr);
    void dispatch_events();

    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);
    bool has_observers() const noexcept {
        return _has_observers.load(std::memory_order_acquire);
    }

    void notify_completed(const AlgorithmOutput& output);
    void append_compact_solution(compact::EnchSolution solution);
    std::vector<compact::EnchSolution> get_solutions() const;

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

    // ── Diagnostic counters (atomic, read from verbose monitor) ─────────
    struct DiagnosticSnapshot {
        int64_t nodes_visited = 0;
        int64_t nodes_pruned = 0;
        int64_t steps_forged = 0;
        int64_t pool_items   = 0;
        double  progress     = 0.0;
        int64_t elapsed_ms   = 0;
    };

    void incr_nodes_visited() noexcept { _diag_nodes_visited.fetch_add(1, std::memory_order_relaxed); }
    void incr_nodes_pruned()   noexcept { _diag_nodes_pruned.fetch_add(1, std::memory_order_relaxed); }
    void incr_steps_forged()   noexcept { _diag_steps_forged.fetch_add(1, std::memory_order_relaxed); }

    DiagnosticSnapshot get_diagnostics(int64_t elapsed_ms = 0) const noexcept {
        return {
            _diag_nodes_visited.load(std::memory_order_relaxed),
            _diag_nodes_pruned.load(std::memory_order_relaxed),
            _diag_steps_forged.load(std::memory_order_relaxed),
            0,
            _progress.load(std::memory_order_acquire),
            elapsed_ms
        };
    }

private:
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

    std::atomic<bool> _has_observers{false};
    SPSCQueue<ObserverEvent, 256> _events; // 256-entry bound: intentional.
    // Non-critical progress events are downsampled; solution events are rare
    // enough that 256 slots are sufficient.  Drops on full are acceptable —
    // observers tolerate occasional missed progress snapshots.

    mutable std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _accum_mtx;
    std::vector<compact::EnchSolution> _accumulated;
    std::atomic<double> _progress{0.0};

    std::atomic<uint32_t> _progress_downsample{0};

    // ── Atomic diagnostic counters ──
    std::atomic<int64_t> _diag_nodes_visited{0};
    std::atomic<int64_t> _diag_nodes_pruned{0};
    std::atomic<int64_t> _diag_steps_forged{0};
};
