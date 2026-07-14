#pragma once
#include "types/AlgorithmTypes.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

class ExecutionContext {
public:
    ExecutionContext() = default;
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    // ═══════════════════════════════════════════════════════════════════
    // 🔴 核心热区 — 每次状态展开调用, 内联, 零堆分配
    // ═══════════════════════════════════════════════════════════════════

    void cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
    void pause() noexcept { _paused.store(true, std::memory_order_release); }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
    }
    bool is_cancelled() const noexcept { return _cancelled.load(std::memory_order_acquire); }
    bool is_paused() const noexcept  { return _paused.load(std::memory_order_acquire); }
    void wait_if_paused();

    void incr_nodes_visited() noexcept { _diag_nodes_visited.fetch_add(1, std::memory_order_relaxed); }
    void incr_nodes_pruned()   noexcept { _diag_nodes_pruned.fetch_add(1, std::memory_order_relaxed); }
    void incr_steps_forged()   noexcept { _diag_steps_forged.fetch_add(1, std::memory_order_relaxed); }
    void incr_pool_items()     noexcept { _diag_pool_items.fetch_add(1, std::memory_order_relaxed); }

    // ═══════════════════════════════════════════════════════════════════
    // 🟡 辅助区 — 定期调用（非热路径）
    // ═══════════════════════════════════════════════════════════════════

    void report_progress(double percent, ProgressStatus status);
    void report_compact_solution(std::vector<compact::EnchStep> solution);

    // ═══════════════════════════════════════════════════════════════════
    // 🟢 可选区 — 执行前后各调用一次
    // ═══════════════════════════════════════════════════════════════════

    void append_compact_solution(compact::EnchSolution solution);
    std::vector<compact::EnchSolution> get_solutions() const;
    double progress() const noexcept { return _progress.load(std::memory_order_acquire); }

    /// 通用诊断 KV 报告（退出点调用, 追加到内部日志）
    void report_diagnostic(std::string_view key, int64_t value);
    void report_diagnostic(std::string_view key, std::string value);

    /// 返回并清空累积的诊断 KV 对（Executor 在 _finalize() 中调用）
    std::vector<std::pair<std::string, std::string>> consume_diagnostic_log();

    // ── Diagnostic snapshot (atomic counters, always available) ─────────────
    struct DiagnosticSnapshot {
        int64_t nodes_visited = 0;
        int64_t nodes_pruned = 0;
        int64_t steps_forged = 0;
        int64_t pool_items   = 0;
        double  progress     = 0.0;
        int64_t elapsed_ms   = 0;
    };

    DiagnosticSnapshot get_diagnostics(int64_t elapsed_ms = 0) const noexcept {
        return {
            _diag_nodes_visited.load(std::memory_order_relaxed),
            _diag_nodes_pruned.load(std::memory_order_relaxed),
            _diag_steps_forged.load(std::memory_order_relaxed),
            _diag_pool_items.load(std::memory_order_relaxed),
            _progress.load(std::memory_order_acquire),
            elapsed_ms
        };
    }

private:
    // ── 🔴 热区数据（同一 cache line） ──────────────────────────────
    alignas(64) std::atomic<bool> _cancelled{false};
    std::atomic<bool>             _paused{false};
    mutable std::mutex            _pause_mtx;
    std::condition_variable       _pause_cv;

    std::atomic<int64_t> _diag_nodes_visited{0};
    std::atomic<int64_t> _diag_nodes_pruned{0};
    std::atomic<int64_t> _diag_steps_forged{0};

    // ── 🟡 辅助区数据 ──────────────────────────────────────────────
    std::atomic<double> _progress{0.0};
    std::vector<std::pair<std::string, std::string>> _diagnostic_log;

    // ── 🟢 可选区数据 ─────────────────────────────────────────────
    std::atomic<int64_t> _diag_pool_items{0};

    mutable std::mutex _accum_mtx;
    std::vector<compact::EnchSolution> _accumulated;
};
