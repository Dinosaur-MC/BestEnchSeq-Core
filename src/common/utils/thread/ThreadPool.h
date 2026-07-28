// SPDX-License-Identifier: MIT
//
// ThreadPool — C++20 compute-oriented thread pool
// =================================================
//
// Design decisions
// ----------------
// • Single global task queue (deque + mutex + condition_variable_any).
//   Sufficient for coarse-grained / static-chunk workloads.  Work-stealing
//   can be layered on top later if profiling shows global-queue contention.
// • Type erasure via unique_ptr<TaskBase> (no std::function copy requirement,
//   so move-only callables just work).
// • pending_ atomic counter enables fast wait() without condition-variable
//   shenanigans in the fast path.
// • jthread + stop_token for lifecycle.  A stop_callback on the cv ensures
//   workers wake up promptly on shutdown.
// • parallel_for() helper matches the project's bread-and-butter use case
//   (static-chunk for-each over an integral index range).
//
// Thread safety
// -------------
// - submit()        : thread-safe, lock-based
// - wait()          : thread-safe, condition-variable-based
// - stop()          : thread-safe, idempotent
// - pending()       : thread-safe, atomic relaxed
// - size()          : thread-safe (read-only after construction)

#pragma once

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------
// Platform helpers
// -----------------------------------------------------------------------

#ifndef BESQ_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE
#  ifdef __cpp_lib_hardware_interference_size
#    define BESQ_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE                               \
        std::hardware_destructive_interference_size
#  else
#    define BESQ_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE 64
#  endif
#endif

namespace besq {

// -----------------------------------------------------------------------
// ThreadPoolMode
// -----------------------------------------------------------------------

enum class ThreadPoolMode {
    /// Single global queue.  Every task goes into one deque protected by a
    /// single mutex.  Workers contend on the mutex but the critical section
    /// is short (just a pointer swap).  Good for chunked / coarse-grained
    /// workloads such as parallel_for.
    SingleQueue,

    /// Per-worker local queue + work stealing (planned, not yet implemented
    /// — currently falls back to SingleQueue behaviour).
    WorkStealing,
};

// -----------------------------------------------------------------------
// ThreadPool
// -----------------------------------------------------------------------

class ThreadPool {
public:
    /// Construct a thread pool with @p num_threads workers (0 =
    /// std::thread::hardware_concurrency, floor 1).
    ///
    /// @param num_threads  Number of worker threads (0 = auto-detect)
    /// @param mode         Scheduling mode
    explicit ThreadPool(
        std::size_t num_threads = 0,
        ThreadPoolMode mode = ThreadPoolMode::SingleQueue);

    // Non-copyable, non-movable.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// Graceful shutdown — drain the queue, join all workers, then destroy.
    ~ThreadPool() { stop(); }

    // -------------------------------------------------------------------
    // Submission
    // -------------------------------------------------------------------

    /// Submit a callable and return a std::future holding its result (or
    /// exception).  The callable must be invocable with no arguments.
    ///
    /// Example:
    ///   auto fut = pool.submit([] { return 42; });
    ///   int result = fut.get();
    template <typename F>
        requires std::invocable<std::decay_t<F>>
    auto submit(F&& f)
        -> std::future<std::invoke_result_t<std::decay_t<F>>>;

    // -------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------

    /// Block until all tasks submitted so far have completed.
    /// Tasks submitted by other threads *during* wait() are NOT waited for.
    void wait();

    /// Graceful stop: no new tasks accepted, pending tasks are drained,
    /// workers exit.  Idempotent — safe to call multiple times.
    void stop() noexcept;

    // -------------------------------------------------------------------
    // Observers
    // -------------------------------------------------------------------

    /// Number of worker threads.
    std::size_t size() const noexcept { return _workers.size(); }

    /// Approximate number of tasks still pending (waiting in queue or
    /// currently executing).  Only meaningful as a hint; the value can
    /// change between the call and its return.
    std::size_t pending() const noexcept {
        return _pending.load(std::memory_order_relaxed);
    }

    /// The scheduling mode this pool was created with.
    ThreadPoolMode mode() const noexcept { return _mode; }

    // -------------------------------------------------------------------
    // Shared pool
    // -------------------------------------------------------------------

    /// Global shared pool (Meyer's singleton).
    /// Configured with hardware_concurrency() workers.
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
            // std::packaged_task stores the exception inside the future;
            // nothing propagates from here.
            pt();
        }
    };

    // ---- Worker helpers ------------------------------------------------

    void _worker_main(std::size_t id, std::stop_token st);
    void _enqueue(std::unique_ptr<TaskBase> task);
    std::unique_ptr<TaskBase> _try_pop_global();
    void _on_task_done() noexcept;

    // ---- Members -------------------------------------------------------

    ThreadPoolMode _mode;

    std::mutex _global_mtx;
    std::deque<std::unique_ptr<TaskBase>> _global_queue;
    std::condition_variable_any _cv;

    std::atomic<std::size_t> _pending{0};
    bool _closed{false};

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

/// Parallel for-each over the integral range [first, last).
///
/// The range is split into @p chunk_count chunks (default: workers * 4).
/// Each chunk runs on a separate submitted task.  The calling thread blocks
/// until all chunks complete.
///
/// If any chunk throws, all other chunks are still allowed to finish
/// (drain semantics), then the first exception is rethrown.
///
/// @param pool        Thread pool to run on
/// @param first       Inclusive lower bound
/// @param last        Exclusive upper bound
/// @param body        Callable  body(Index i)  invoked for each index
/// @param chunk_size  Explicit chunk size (0 = auto)
///
/// Example:
///   parallel_for(pool, 0uz, 1'000'000, [&](std::size_t i) {
///       data[i] = std::sqrt(data[i]);
///   });
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

    // Determine chunk count.
    // Default: workers * 4 gives good load-balancing for typical workloads.
    const std::size_t target_chunks = std::max<std::size_t>(1, workers * 4);
    Index step = chunk_size;
    if (step <= 0) {
        step = static_cast<Index>(
            (static_cast<diff_t>(n) + static_cast<diff_t>(target_chunks) - 1)
            / static_cast<diff_t>(target_chunks));
        if (step < 1) step = 1;
    }

    // Pre-compute chunk boundaries.
    std::size_t num_chunks = 0;
    {
        Index pos = first;
        while (pos < last) {
            ++num_chunks;
            pos += step;
        }
    }

    // Submit all chunks, collect futures.
    std::vector<std::future<void>> futures(num_chunks);

    try {
        Index pos = first;
        for (std::size_t ci = 0; ci < num_chunks; ++ci) {
            Index chunk_begin = pos;
            Index chunk_end = (last - pos < step) ? last : pos + step;
            futures[ci] = pool.submit([&body, chunk_begin, chunk_end] {
                for (Index i = chunk_begin; i < chunk_end; ++i) {
                    body(i);
                }
            });
            pos = chunk_end;
        }
    } catch (...) {
        // Submission failed (e.g. pool already stopped).
        // Wait for already-submitted chunks before rethrowing.
        for (auto& f : futures) {
            if (f.valid()) {
                try { f.get(); } catch (...) { /* ignore */ }
            }
        }
        throw;
    }

    // Collect results / first exception.
    std::exception_ptr first_error;
    for (auto& f : futures) {
        try {
            f.get();
        } catch (...) {
            if (!first_error) first_error = std::current_exception();
        }
    }
    if (first_error) std::rethrow_exception(first_error);
}

} // namespace besq
