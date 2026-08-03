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

/// Abstract execution-control wakeup.  SandboxedAlgorithm derives a concrete
/// one that OWNS the eventfd / Win32 event and closes it on destruction, so
/// cancel()/pause()/resume() can wake the IPC wait without leaking the handle
/// or racing its lifetime (a single atomic shared_ptr keeps the notifier alive
/// while a notify() is in flight).
class ControlNotifier {
  public:
    virtual ~ControlNotifier() = default;
    virtual void notify() noexcept = 0;
};

class ExecutionContext {
  public:
    ExecutionContext(size_t task_id, const char *algorithm_name) noexcept;
    ExecutionContext(const ExecutionContext &)            = delete;
    ExecutionContext &operator=(const ExecutionContext &) = delete;

    // ─── 预缓存常量 ──────────────────────────────────────────────────
    size_t task_id() const noexcept { return _task_id; }
    const char *algorithm_name() const noexcept { return _algo_name; }

    // ─── 恢复标记 — 由 Executor 在 start(checkpoint) 中设置 ────────────
    bool is_restored() const noexcept { return _restored; }
    void set_restored(bool v) noexcept { _restored = v; }

    // ═══════════════════════════════════════════════════════════════════
    // 执行控制
    // ═══════════════════════════════════════════════════════════════════
    /// Execution-control notifier: SandboxedAlgorithm installs one so its IPC
    /// wait wakes the INSTANT the executor's control state changes — cancel(),
    /// pause() or resume() — instead of polling.  A single atomic shared_ptr
    /// keeps the notifier alive across the handoff: no torn fn/ud pair, no
    /// use-after-free when it is reset while a notify() is in flight (the
    /// notifier frees its fd/event only when the last ref drops).  In-process
    /// algorithms never install one: each control call then does one extra
    /// null load — nothing on the hot path.
    void set_control_notifier(std::shared_ptr<ControlNotifier> n) noexcept {
        _control_notifier.store(std::move(n), std::memory_order_release);
    }
    void cancel() noexcept {
        _cancelled.store(true, std::memory_order_release);
        auto n = _control_notifier.load(std::memory_order_acquire);
        if (n)
            n->notify();
    }
    void pause() noexcept {
        _paused.store(true, std::memory_order_release);
        auto n = _control_notifier.load(std::memory_order_acquire);
        if (n)
            n->notify();
    }
    void resume() noexcept {
        _paused.store(false, std::memory_order_release);
        _pause_cv.notify_all();
        auto n = _control_notifier.load(std::memory_order_acquire);
        if (n)
            n->notify();
    }
    bool is_cancelled() const noexcept { return _cancelled.load(std::memory_order_acquire); }
    bool is_paused() const noexcept { return _paused.load(std::memory_order_acquire); }
    void wait_if_paused();

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
    void report_solution(const std::vector<EnchStep> &steps); // lvalue → 1 copy
    void report_solution(std::vector<EnchStep> &&steps);      // rvalue → move (0 copy)

    // ═══════════════════════════════════════════════════════════════════
    // 🟢 退出诊断 — 执行前后各调用一次
    // ═══════════════════════════════════════════════════════════════════
    void set_exit_diagnostics(std::unique_ptr<AlgorithmDiagnostics> d) { _exit_diag = std::move(d); }
    // Convenience: deduces concrete type from _diag member, algorithm just writes
    //   ctx.set_exit_diagnostics(_diag);
    template <typename T> void set_exit_diagnostics(T &diag) {
        set_exit_diagnostics(std::make_unique<T>(std::move(diag)));
    }
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
        return {
            .nodes_visited = _nodes_visited.load(std::memory_order_relaxed),
            .nodes_pruned  = _nodes_pruned.load(std::memory_order_relaxed),
            .steps_forged  = _steps_forged.load(std::memory_order_relaxed),
            .progress      = static_cast<double>(_progress.load(std::memory_order_acquire)),
            .elapsed_ms    = elapsed_ms
        };
    }

  private:
    void append_solution(std::shared_ptr<const EnchSolution> solution);

    // ── 预缓存常量 ────────────────────────────────────────────────────
    size_t _task_id;
    const char *_algo_name;

    // ── 执行控制 ──────────────────────────────────────────────────────
    alignas(64) std::atomic<bool> _cancelled{false};
    std::atomic<bool> _paused{false};
    mutable std::mutex _pause_mtx;
    std::condition_variable _pause_cv;

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

    // ── 执行控制通知（SandboxedAlgorithm 专用）────────────────────────
    // 追加在类末尾：既有成员偏移不变，已编译插件（旧 ABI）不会因本类
    // 布局变化而读写错位（2026-08-03 曾因字段插中间导致插件 DLL 段错误）。
    std::atomic<std::shared_ptr<ControlNotifier>> _control_notifier{nullptr};
};

} // namespace algorithm
