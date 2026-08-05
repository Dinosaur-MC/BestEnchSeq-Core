#pragma once
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/IExecutor.h"
#include "domain/algorithm/types/AlgorithmState.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace algorithm {
class EnchReg;

// Forward declarations
struct AlgorithmOutput;
class IAlgorithm;
class DiagnosticsService;

// ─── AlgorithmExecutor (async execution engine, compact-only) ───
class AlgorithmExecutor : public IExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    // ── IExecutor metadata / preflight ─────────────────────────────────
    // Thin forwarders to the owned algorithm (this executor always owns one;
    // the constructor throws on null).
    std::string_view name() const noexcept override { return _algorithm->name(); }
    std::string_view version() const noexcept override { return _algorithm->version(); }
    AlgorithmMode supported_mode() const noexcept override { return _algorithm->supported_mode(); }
    bool simulate(const AlgorithmInput& input) const noexcept override { return _algorithm->simulate(input); }
    /// Predicted wall-clock solve time for \p ench_count enchantments (seconds).
    double evaluate(int16_t ench_count) const noexcept { return _algorithm->evaluate(ench_count); }

    /// IExecutor: fresh run (no warmup).
    void start(AlgorithmInput input) override;
    /// Run with a synchronous warmup phase (benchmark-only; distinct signature
    /// from the virtual — no default arg so the two never collide at call sites).
    void start(AlgorithmInput input, std::unique_ptr<IAlgorithm> warmup);

    /// Start execution from a self-contained checkpoint.
    /// The checkpoint must contain all AlgorithmInput data and algorithm state.
    /// Throws std::invalid_argument on empty checkpoint.
    /// Throws std::logic_error if algorithm doesn't support serialization.
    /// Throws std::runtime_error if checkpoint deserialization fails.
    void start(const std::vector<uint8_t>& checkpoint) override;

    void pause() override;
    void resume() override;
    void cancel() override;
    /// Explicitly return a terminal (Completed/Failed/Cancelled) executor to
    /// Idle so the same instance can start() again (the sandbox worker reuses
    /// one executor across MsgRun frames).  Refuses while Running/Paused —
    /// callers must wait() first.  Implicit transitions FROM terminal states
    /// stay blocked (_set_state), only this explicit reset re-arms the run.
    bool reset() noexcept;
    AlgorithmState wait() override;
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    AlgorithmState state() const noexcept override;
    double progress() const noexcept override;

    AlgorithmOutput output() const override;

    ExecutionContext::Snapshot get_diagnostics(int64_t elapsed_ms = 0) const {
        return _ctx ? _ctx->get_diagnostics(elapsed_ms) : ExecutionContext::Snapshot{};
    }

    std::vector<uint8_t> serialize_state() const override;
    bool restore_state(const std::vector<uint8_t>& data);
    bool is_serializable() const noexcept override;

    /// Returns the error message from a failed execution, if any.
    const std::string& error_message() const noexcept { return _error_message; }

private:
    void _join_worker() noexcept;
    /// Atomically transition to new_state. Returns true if state changed.
    /// Skips if already at new_state (noop) or if FROM a terminal state.
    bool _set_state(AlgorithmState new_state) noexcept;
    void _run_warmup(AlgorithmInput& input, IAlgorithm& warmup_algo);
    void _finalize();
    void _start_timeout_watcher(std::chrono::milliseconds max_time);
    void _stop_timeout_watcher() noexcept;
    /// Record elapsed time since _start_time.  Set from the worker thread
    /// immediately after execute(); _finalize() preserves this value instead
    /// of re-measuring, so reported times exclude teardown overhead.
    void _record_computation_time() noexcept;

    std::unique_ptr<IAlgorithm> _algorithm;
    std::unique_ptr<ExecutionContext> _ctx;
    std::optional<std::thread> _worker;
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    /// A cancel() that landed while Idle (pre-start publish-window race: the
    /// solve pipeline can be observed through the active-executor handle
    /// before start() flips the state) must not be lost — the next start()
    /// consumes this flag and poisons the ExecutionContext so the run bails
    /// at its first cancellation check.
    std::atomic<bool> _cancel_pending{false};
    AlgorithmInput _algorithm_input; // owned by executor, passed to serializer
    std::atomic<bool> _finalized{false};
    std::mutex _state_mtx;
    std::condition_variable _state_cv;
    std::chrono::steady_clock::time_point _start_time;
    std::chrono::milliseconds _computation_time{0};
    /// True once the worker thread has recorded _computation_time.  A plain
    /// bool is safe: the write happens-before _finalize() via the thread join.
    bool _computation_time_recorded = false;
    static inline std::atomic<size_t> _next_task_id{1}; // 0 = invalid
    size_t _task_id{0};
    std::string _algo_name_cache;
    std::string _error_message; // captured from worker thread exception

    // Timeout watcher: background thread that cancels context when time is up.
    // The condition variable lets _stop_timeout_watcher() wake it immediately
    // instead of waiting out a 10ms poll interval on join.
    std::shared_ptr<std::atomic<bool>> _timeout_alive;
    std::optional<std::thread> _timeout_watcher;
    std::mutex _timeout_mtx;
    std::condition_variable _timeout_cv;
};
} // namespace algorithm
