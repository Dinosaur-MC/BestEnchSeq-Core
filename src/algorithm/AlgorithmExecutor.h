#pragma once
#include "ExecutionContext.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// Forward declarations (full definitions in IAlgorithm.h, included in .cpp)
struct AlgorithmOutput;
struct AlgorithmInput;
class IAlgorithm;

// ─── AlgorithmExecutor (async execution engine) ───
class AlgorithmExecutor {
public:
    explicit AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm);
    ~AlgorithmExecutor();

    // Non-copyable, non-movable
    AlgorithmExecutor(const AlgorithmExecutor&) = delete;
    AlgorithmExecutor& operator=(const AlgorithmExecutor&) = delete;

    // Lifecycle
    void start(const AlgorithmInput& input);
    // Start with a previously-serialized state to resume a paused search.
    // The algorithm must be resumable (DFS) and the state must have been
    // produced by serialize_state() from the same algorithm type.
    void start(const AlgorithmInput& input, const std::vector<uint8_t>& previous_state);
    void pause();
    void resume();
    void cancel();
    AlgorithmState wait();
    AlgorithmState wait_for(std::chrono::milliseconds timeout);

    // State queries
    AlgorithmState state() const noexcept;
    double progress() const noexcept;

    // Observer
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    // Result
    AlgorithmOutput output() const;

    // Serialization
    // Captures algorithm search state (best solution, visited set) for
    // cross-session checkpointing. Only works for resumable algorithms (DFS).
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
};
