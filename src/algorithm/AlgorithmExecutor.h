#pragma once
#include "IAlgorithm.h"
#include "ExecutionContext.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace compact { class EnchReg; }

// Forward declarations (full definitions in IAlgorithm.h)
struct AlgorithmOutput;
class IAlgorithm;

// ─── AlgorithmExecutor (async execution engine, compact-only) ───
class AlgorithmExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    /// Start with compact AlgorithmInput.
    void start(AlgorithmInput input);

    /// Resume from a previously-serialized state.
    void start(AlgorithmInput input, const std::vector<uint8_t>& previous_state);

    void pause();
    void resume();
    void cancel();
    AlgorithmState wait();
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    AlgorithmState state() const noexcept;
    double progress() const noexcept;

    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    // Result (returns compact AlgorithmOutput; caller converts to domain if needed)
    AlgorithmOutput output() const;

    void update_search_config(ExecutionContext::SearchConfig cfg);

    // Serialization
    std::vector<uint8_t> serialize_state() const;
    bool restore_state(const std::vector<uint8_t>& data);

private:
    void _join_worker() noexcept;
    void _set_state(AlgorithmState new_state) noexcept;

    std::unique_ptr<IAlgorithm> _algorithm;
    std::unique_ptr<ExecutionContext> _ctx;
    std::optional<std::thread> _worker;
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    std::mutex _state_mtx;
    std::condition_variable _state_cv;
    std::chrono::steady_clock::time_point _start_time;
    std::chrono::milliseconds _computation_time{0};

    // Boundary data for output conversion
    const Equipment* _out_equipment{nullptr};
};
