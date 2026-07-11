#pragma once
#include "AlgorithmObserver.h"
#include "utils/SPSCQueue.hpp"
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
    void append_compact_steps(const std::vector<compact::EnchStep>& steps);
    std::vector<std::vector<compact::EnchStep>> get_accumulated_compact_steps() const;

    double progress() const noexcept {
        return _progress.load(std::memory_order_acquire);
    }

    // ── Search config ──
    struct SearchConfig {
        int32_t max_solutions = 0;
        int32_t max_depth = 0;
        int32_t memory_mb = 0;
        std::chrono::milliseconds max_search_time{0};
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
    SPSCQueue<ObserverEvent, 256> _events;

    mutable std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    mutable std::mutex _accum_mtx;
    std::vector<std::pair<int32_t, std::vector<compact::EnchStep>>> _accumulated;
    std::atomic<double> _progress{0.0};

    std::atomic<uint32_t> _progress_downsample{0};

    std::atomic<std::shared_ptr<const SearchConfig>> _search_config{nullptr};

    // ── Atomic diagnostic counters ──
    std::atomic<int64_t> _diag_nodes_visited{0};
    std::atomic<int64_t> _diag_nodes_pruned{0};
    std::atomic<int64_t> _diag_steps_forged{0};
};
