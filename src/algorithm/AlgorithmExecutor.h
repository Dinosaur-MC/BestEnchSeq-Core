#pragma once
#include "IAlgorithm.h"
#include "ExecutionContext.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace compact { class EnchReg; }

// Forward declarations
struct AlgorithmOutput;
class IAlgorithm;
class DiagnosticsService;

// ─── AlgorithmExecutor (async execution engine, compact-only) ───
class AlgorithmExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    void start(AlgorithmInput input, std::unique_ptr<IAlgorithm> warmup = nullptr);

    /// Start execution from a self-contained checkpoint.
    /// The checkpoint must contain all AlgorithmInput data and algorithm state.
    /// Throws std::invalid_argument on empty checkpoint.
    /// Throws std::logic_error if algorithm doesn't support serialization.
    /// Throws std::runtime_error if checkpoint deserialization fails.
    void start(const std::vector<uint8_t>& checkpoint);

    void pause();
    void resume();
    void cancel();
    AlgorithmState wait();
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    AlgorithmState state() const noexcept;
    double progress() const noexcept;

    AlgorithmOutput output() const;

    ExecutionContext::Snapshot get_diagnostics(int64_t elapsed_ms = 0) const {
        return _ctx ? _ctx->get_diagnostics(elapsed_ms) : ExecutionContext::Snapshot{};
    }

    std::vector<uint8_t> serialize_state() const;
    bool restore_state(const std::vector<uint8_t>& data);
    bool is_serializable() const noexcept;

    /// Returns the error message from a failed execution, if any.
    const std::string& error_message() const noexcept { return _error_message; }

private:
    void _join_worker() noexcept;
    /// Atomically transition to new_state. Returns true if state changed.
    /// Skips if already at new_state (noop) or if FROM a terminal state.
    bool _set_state(AlgorithmState new_state) noexcept;
    void _run_warmup(AlgorithmInput& input, IAlgorithm& warmup_algo);
    void _finalize();

    std::unique_ptr<IAlgorithm> _algorithm;
    std::unique_ptr<ExecutionContext> _ctx;
    std::optional<std::thread> _worker;
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    AlgorithmInput _algorithm_input;  // owned by executor, passed to serializer
    std::atomic<bool> _finalized{false};
    std::mutex _state_mtx;
    std::condition_variable _state_cv;
    std::chrono::steady_clock::time_point _start_time;
    std::chrono::milliseconds _computation_time{0};
    static inline std::atomic<size_t> _next_task_id{1};  // 0 = invalid
    size_t _task_id{0};
    std::string _algo_name_cache;
    std::string _error_message;         // captured from worker thread exception
};
