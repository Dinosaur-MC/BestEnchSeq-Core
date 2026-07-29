// SPDX-License-Identifier: MIT
//
// ThreadPool — C++20 compute-oriented thread pool
// =================================================
//
// Queue   : SegmentedMPMCQueue<TaskPtr> — lock-free MPMC, no mutex contention
// Wake    : Monotonic std::atomic<uint64_t> wake counter + C++20
//           atomic::wait / notify_all.  Submitters only notify when the pool
//           transitions from idle (prev == 0), eliminating the futex syscall
//           storm that would come from notifying on every submit.
// Workers : Short spin before sleep handles burst submissions without
//           kernel transitions.
//
// Thread safety
// -------------
// - submit()   : thread-safe, lock-free
// - wait()     : thread-safe, atomic wait
// - stop()     : thread-safe, idempotent
// - pending()  : thread-safe, atomic relaxed
// - size()     : thread-safe (read-only after construction)

#pragma once

#include "utils/queue/SegmentedMPMCQueue.hpp"

#include <atomic>
#include <concepts>
#include <exception>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace besq {

// -----------------------------------------------------------------------
// ThreadPool
// -----------------------------------------------------------------------

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 0);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() { stop(); }

    // -------------------------------------------------------------------
    // Submission
    // -------------------------------------------------------------------

    template <typename F>
        requires std::invocable<std::decay_t<F>>
    auto submit(F&& f)
        -> std::future<std::invoke_result_t<std::decay_t<F>>>;

    // -------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------

    void wait();
    void stop() noexcept;

    // -------------------------------------------------------------------
    // Observers
    // -------------------------------------------------------------------

    std::size_t size() const noexcept { return _workers.size(); }
    std::size_t pending() const noexcept {
        return _pending.load(std::memory_order_relaxed);
    }

    // -------------------------------------------------------------------
    // Shared pool
    // -------------------------------------------------------------------

    static ThreadPool& shared();

private:
    // ---- Type erasure -------------------------------------------------

    struct TaskBase {
        virtual ~TaskBase() = default;
        virtual void run() = 0;
    };

    template <typename R>
    struct Task final : TaskBase {
        std::packaged_task<R()> pt;

        explicit Task(std::packaged_task<R()> p) : pt(std::move(p)) {}

        void run() noexcept override {
            // packaged_task::operator() catches exceptions from the
            // stored callable and stores them in the associated future,
            // so this should never propagate.  The outer try/catch is
            // a defensive barrier against hypothetical corner cases
            // (e.g. future_error from an invalid shared state).
            try { pt(); } catch (...) { /* swallowed — see future */ }
        }
    };

    // ---- Task pointer type & lock-free queue --------------------------

    using TaskPtr = std::unique_ptr<TaskBase>;
    using Queue   = SegmentedMPMCQueue<TaskPtr>;

    Queue _queue;

    // Monotonic wake counter — strictly increases on:
    //   • idle→active transition in _enqueue (new task when prev==0)
    //   • last-task-completed in _on_task_done (prev==1, wakes wait())
    //   • stop()
    //
    // Separated by cache-line padding to avoid false-sharing with
    // _pending and _stopped (both hammered by different threads).
    alignas(64) std::atomic<uint64_t> _wake_counter{0};

    // ---- Constants -----------------------------------------------------

    /// How many tasks a worker pulls from the shared queue at once.
    /// Higher values reduce CAS contention on the queue but increase
    /// latency imbalance.  Power-of-two for cheap mask arithmetic.
    static constexpr size_t kBatchSize = 4;

    // ---- Worker helpers ------------------------------------------------

    void _worker_main(std::size_t id, std::stop_token st);
    void _enqueue(TaskPtr task);
    void _on_task_done() noexcept;

    // ---- Members -------------------------------------------------------

    alignas(64) std::atomic<std::size_t> _pending{0};
    alignas(64) std::atomic<bool> _stopped{false};
    std::vector<std::jthread> _workers;
};

// =======================================================================
// Template implementations
// =======================================================================

template <typename F>
    requires std::invocable<std::decay_t<F>>
auto ThreadPool::submit(F&& f)
    -> std::future<std::invoke_result_t<std::decay_t<F>>> {

    using R = std::invoke_result_t<std::decay_t<F>>;

    std::packaged_task<R()> task(std::forward<F>(f));
    std::future<R> future = task.get_future();

    _enqueue(std::make_unique<Task<R>>(std::move(task)));
    return future;
}

// =======================================================================
// parallel_for  —  static chunk helper
// =======================================================================

template <typename Index, typename Body>
    requires std::integral<Index> && std::invocable<Body&, Index>
void parallel_for(ThreadPool& pool,
                  Index first,
                  Index last,
                  Body&& body,
                  Index chunk_size = 0) {
    if (first >= last) return;

    using diff_t = std::make_signed_t<Index>;
    const Index n = last - first;
    const std::size_t workers = pool.size();

    const std::size_t target_chunks = std::max<std::size_t>(1, workers * 4);
    Index step = chunk_size;
    if (step <= 0) {
        step = static_cast<Index>(
            (static_cast<diff_t>(n) + static_cast<diff_t>(target_chunks) - 1)
            / static_cast<diff_t>(target_chunks));
        if (step < 1) step = 1;
    }

    std::size_t num_chunks = 0;
    { Index pos = first; while (pos < last) { ++num_chunks; pos += step; } }

    std::vector<std::future<void>> futures(num_chunks);

    try {
        Index pos = first;
        for (std::size_t ci = 0; ci < num_chunks; ++ci) {
            Index chunk_begin = pos;
            Index chunk_end = (last - pos < step) ? last : pos + step;
            futures[ci] = pool.submit([&body, chunk_begin, chunk_end] {
                for (Index i = chunk_begin; i < chunk_end; ++i) body(i);
            });
            pos = chunk_end;
        }
    } catch (...) {
        for (auto& f : futures)
            if (f.valid()) { try { f.get(); } catch (...) { } }
        throw;
    }

    std::exception_ptr first_error;
    for (auto& f : futures) {
        try { f.get(); }
        catch (...) { if (!first_error) first_error = std::current_exception(); }
    }
    if (first_error) std::rethrow_exception(first_error);
}

} // namespace besq
