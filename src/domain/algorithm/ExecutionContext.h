#pragma once
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/types/Solution.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#ifndef BESQ_MAX_SOLUTIONS
#define BESQ_MAX_SOLUTIONS 128
#endif

namespace algorithm {

class ExecutionContext {
public:
    ExecutionContext(size_t task_id, const char* algorithm_name) noexcept;
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    // ─── 预缓存常量 ──────────────────────────────────────────────────
    size_t task_id() const noexcept { return _task_id; }
    const char* algorithm_name() const noexcept { return _algo_name; }

    // ─── 恢复标记 — 由 Executor 在 start(checkpoint) 中设置 ────────────
    bool is_restored() const noexcept { return _restored; }
    void set_restored(bool v) noexcept { _restored = v; }

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
    bool is_paused() const noexcept { return _paused.load(std::memory_order_acquire); }
    void wait_if_paused();

    // ─── 暂停确认（checkpoint 一致性）────────────────────────────────
    // pause() 把 executor 状态置为 Paused 是即时的，但算法只在
    // wait_if_paused() 处真正停下——它可能仍处在一个不检查暂停的长阶段
    // （如 greedy/dfs 上界搜索）里改动搜索状态。若此刻快照 checkpoint，
    // 会得到一个不一致的状态（resume 后从空 open-set 起跑 → 0 方案）。
    // wait_if_paused() 真正阻塞时置位该 ack，executor 的 serialize_state()
    // 等待它，保证快照点是静止的。
    bool is_paused_acked() const noexcept { return _paused_ack.load(std::memory_order_acquire); }
    std::mutex &pause_ack_mutex() const noexcept { return _pause_ack_mtx; }
    std::condition_variable &pause_ack_cv() const noexcept { return _pause_ack_cv; }
    void notify_pause_ack() const noexcept { _pause_ack_cv.notify_all(); }

    // ═══════════════════════════════════════════════════════════════════
    // 🔴 热路径 — 每次展开调用, 内联, 零堆分配
    // ═══════════════════════════════════════════════════════════════════
    // Per-operation counters are TIER 2 (spec §5): off by default, enabled
    // only in profiling builds (-DBESQ_DEEP_DIAGNOSTICS).  When off these are
    // empty inline functions, so the compiler eliminates every call site —
    // zero instructions, zero atomics, zero contention.  (relaxed atomics are
    // not free: 32 threads writing the same cacheline cost ~1.5s at sword_16.)
#if defined(BESQ_DEEP_DIAGNOSTICS)
    void incr_nodes_visited() noexcept { _nodes_visited.fetch_add(1, std::memory_order_relaxed); }
    void incr_nodes_pruned() noexcept { _nodes_pruned.fetch_add(1, std::memory_order_relaxed); }
    void incr_steps_forged() noexcept { _steps_forged.fetch_add(1, std::memory_order_relaxed); }
#else
    void incr_nodes_visited() noexcept {}
    void incr_nodes_pruned() noexcept {}
    void incr_steps_forged() noexcept {}
#endif

    // ═══════════════════════════════════════════════════════════════════
    // 🟡 流式通知 — 直接内部调 DiagnosticsService::push
    // ═══════════════════════════════════════════════════════════════════
    void report_progress(uint8_t pct, ProgressStatus status);
    void report_solution(const std::vector<EnchStep>& steps); // lvalue → 1 copy
    void report_solution(std::vector<EnchStep>&& steps);      // rvalue → move (0 copy)

    // ═══════════════════════════════════════════════════════════════════
    // 🟢 退出诊断 — 执行前后各调用一次
    // ═══════════════════════════════════════════════════════════════════
    void set_exit_diagnostics(std::unique_ptr<AlgorithmDiagnostics> d) { _exit_diag = std::move(d); }
    // Convenience: deduces concrete type from _diag member, algorithm just writes
    //   ctx.set_exit_diagnostics(_diag);
    template <typename T> void set_exit_diagnostics(T& diag) { set_exit_diagnostics(std::make_unique<T>(std::move(diag))); }
    std::unique_ptr<AlgorithmDiagnostics> consume_exit_diagnostics() { return std::move(_exit_diag); }

    // ─── 输出 ─────────────────────────────────────────────────────────
    /// Returns progress as 0.0–1.0 (converted from internal uint8_t 0–100).
    double progress() const noexcept { return _progress.load(std::memory_order_acquire) / 100.0; }
    std::vector<EnchSolution> get_solutions() const;

    struct Snapshot {
        int64_t nodes_visited, nodes_pruned, steps_forged;
        double progress;
        int64_t elapsed_ms;
    };

    Snapshot get_diagnostics(int64_t elapsed_ms = 0) const noexcept {
        return {.nodes_visited = _nodes_visited.load(std::memory_order_relaxed),
                .nodes_pruned = _nodes_pruned.load(std::memory_order_relaxed),
                .steps_forged = _steps_forged.load(std::memory_order_relaxed),
                .progress = static_cast<double>(_progress.load(std::memory_order_acquire)),
                .elapsed_ms = elapsed_ms};
    }

private:
    void append_solution(std::shared_ptr<const EnchSolution> solution);

    // ── 预缓存常量 ────────────────────────────────────────────────────
    size_t _task_id;
    const char* _algo_name;

    // ── 执行控制 ──────────────────────────────────────────────────────
    alignas(64) std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

    // ── 暂停确认状态（见 is_paused_acked 注释）──────────────────────
    std::atomic<bool> _paused_ack{false};
    mutable std::mutex _pause_ack_mtx;
    mutable std::condition_variable _pause_ack_cv;

    // ── 热路径计数器 ─────────────────────────────────────────────────
    std::atomic<int64_t> _nodes_visited{0};
    std::atomic<int64_t> _nodes_pruned{0};
    std::atomic<int64_t> _steps_forged{0};

    // ── 进度 + 限频 ──────────────────────────────────────────────────
    std::atomic<uint8_t> _progress{0};
    std::atomic<int8_t> _progress_pct{-1};

    // ── 恢复标记 — 由 Executor 在 start(checkpoint) 中设置 ────────────
    bool _restored{false};

    // ── 解法累积 ─────────────────────────────────────────────────────
    mutable std::mutex _sol_mtx;
    std::vector<std::shared_ptr<const EnchSolution>> _solutions;

    // ── 退出诊断 ──────────────────────────────────────────────────────
    std::unique_ptr<AlgorithmDiagnostics> _exit_diag;
};

} // namespace algorithm
