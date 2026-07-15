#pragma once
#include "types/AlgorithmTypes.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <memory>
#include <vector>
#include "diagnostics/AlgorithmDiagnostics.h"

#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

class ExecutionContext {
public:
    ExecutionContext(size_t task_id, const char* algorithm_name) noexcept;
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    // ─── 预缓存常量 ──────────────────────────────────────────────────
    size_t task_id() const noexcept { return _task_id; }
    const char* algorithm_name() const noexcept { return _algo_name; }

    // ═══════════════════════════════════════════════════════════════════
    // 执行控制
    // ═══════════════════════════════════════════════════════════════════
    void cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
    void pause() noexcept { _paused.store(true, std::memory_order_release); }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
    }
    bool is_cancelled() const noexcept { return _cancelled.load(std::memory_order_acquire); }
    void wait_if_paused();

    // ═══════════════════════════════════════════════════════════════════
    // 🔴 热路径 — 每次展开调用, 内联, 零堆分配
    // ═══════════════════════════════════════════════════════════════════
    void incr_nodes_visited() noexcept { _nodes_visited.fetch_add(1, std::memory_order_relaxed); }
    void incr_nodes_pruned()   noexcept { _nodes_pruned.fetch_add(1, std::memory_order_relaxed); }
    void incr_steps_forged()   noexcept { _steps_forged.fetch_add(1, std::memory_order_relaxed); }

    // ═══════════════════════════════════════════════════════════════════
    // 🟡 流式通知 — 直接内部调 DiagnosticsService::push
    // ═══════════════════════════════════════════════════════════════════
    void report_progress(uint8_t pct, ProgressStatus status);
    void report_solution(const std::vector<compact::EnchStep>& steps);   // lvalue → 1 copy
    void report_solution(std::vector<compact::EnchStep>&& steps);        // rvalue → move (0 copy)

    // ═══════════════════════════════════════════════════════════════════
    // 🟢 退出诊断 — 执行前后各调用一次
    // ═══════════════════════════════════════════════════════════════════
    void set_exit_diagnostics(std::unique_ptr<AlgorithmDiagnostics> d) {
        _exit_diag = std::move(d);
    }
    // Convenience: deduces concrete type from _diag member, algorithm just writes
    //   ctx.set_exit_diagnostics(_diag);
    template <typename T>
    void set_exit_diagnostics(T& diag) {
        set_exit_diagnostics(std::make_unique<T>(std::move(diag)));
    }
    std::unique_ptr<AlgorithmDiagnostics> consume_exit_diagnostics() {
        return std::move(_exit_diag);
    }

    // ─── 输出 ─────────────────────────────────────────────────────────
    /// Returns progress as 0.0–1.0 (converted from internal uint8_t 0–100).
    double progress() const noexcept { return _progress.load(std::memory_order_acquire) / 100.0; }
    std::vector<compact::EnchSolution> get_solutions() const;

    struct Snapshot {
        int64_t nodes_visited, nodes_pruned, steps_forged;
        double progress; int64_t elapsed_ms;
    };

    Snapshot get_diagnostics(int64_t elapsed_ms = 0) const noexcept {
        return {
            .nodes_visited = _nodes_visited.load(std::memory_order_relaxed),
            .nodes_pruned  = _nodes_pruned.load(std::memory_order_relaxed),
            .steps_forged  = _steps_forged.load(std::memory_order_relaxed),
            .progress      = static_cast<double>(_progress.load(std::memory_order_acquire)),
            .elapsed_ms    = elapsed_ms
        };
    }

private:
    void append_solution(std::shared_ptr<const compact::EnchSolution> solution);

    // ── 预缓存常量 ────────────────────────────────────────────────────
    size_t _task_id;
    const char* _algo_name;

    // ── 执行控制 ──────────────────────────────────────────────────────
    alignas(64) std::atomic<bool> _cancelled{false};
    std::atomic<bool>             _paused{false};
    mutable std::mutex            _pause_mtx;
    std::condition_variable       _pause_cv;

    // ── 热路径计数器 ─────────────────────────────────────────────────
    std::atomic<int64_t> _nodes_visited{0};
    std::atomic<int64_t> _nodes_pruned{0};
    std::atomic<int64_t> _steps_forged{0};

    // ── 进度 + 限频 ──────────────────────────────────────────────────
    std::atomic<uint8_t> _progress{0};
    std::atomic<int8_t> _progress_pct{-1};

    // ── 解法累积 ─────────────────────────────────────────────────────
    mutable std::mutex _sol_mtx;
    std::vector<std::shared_ptr<const compact::EnchSolution>> _solutions;

    // ── 退出诊断 ──────────────────────────────────────────────────────
    std::unique_ptr<AlgorithmDiagnostics> _exit_diag;
};
