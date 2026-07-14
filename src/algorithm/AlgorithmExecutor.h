#pragma once
#include "IAlgorithm.h"
#include "ExecutionContext.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// ─── Observer dispatch period ───────────────────────────────────────────
#ifndef BESQ_DISPATCH_MS
#define BESQ_DISPATCH_MS 50
#endif

namespace compact { class EnchReg; }

// Forward declarations
struct AlgorithmOutput;
class IAlgorithm;

// ─── AlgorithmExecutor (async execution engine, compact-only) ───
class AlgorithmExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    void start(AlgorithmInput input, std::unique_ptr<IAlgorithm> warmup = nullptr);
    void start(AlgorithmInput input, const std::vector<uint8_t>& previous_state);

    void pause();
    void resume();
    void cancel();
    AlgorithmState wait();
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    AlgorithmState state() const noexcept;
    double progress() const noexcept;

    AlgorithmOutput output() const;

    ExecutionContext::DiagnosticSnapshot get_diagnostics(int64_t elapsed_ms = 0) const {
        return _ctx ? _ctx->get_diagnostics(elapsed_ms) : ExecutionContext::DiagnosticSnapshot{};
    }

    std::vector<uint8_t> serialize_state() const;
    bool restore_state(const std::vector<uint8_t>& data);

private:
    void _join_worker() noexcept;
    void _set_state(AlgorithmState new_state) noexcept;
    void _run_warmup(AlgorithmInput& input, IAlgorithm& warmup_algo);
    void _finalize();

    std::unique_ptr<IAlgorithm> _algorithm;
    std::unique_ptr<ExecutionContext> _ctx;
    std::optional<std::thread> _worker;
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    std::mutex _state_mtx;
    std::condition_variable _state_cv;
    std::chrono::steady_clock::time_point _start_time;
    std::chrono::milliseconds _computation_time{0};
};
