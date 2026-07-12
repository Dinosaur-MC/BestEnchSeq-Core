#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

#include "queue/BoundedMPMCQueue.hpp"
#include "queue/SegmentedMPMCQueue.hpp"
#include "queue/SPSCQueue.hpp"

// ─── EventLoop ────────────────────────────────────────────────────────────
// Zero-CPU-idle task event loop built on C++20 atomic wait/notify.
//
// A single consumer thread processes tasks submitted from any number of
// producer threads.  When the queue is empty the consumer blocks in
// std::atomic<uint64_t>::wait — zero CPU, zero polling, immediate wake
// on the next post().
//
// ── Template parameter ────────────────────────────────────────────────
//   Queue — a lock-free queue type from this project:
//             SegmentedMPMCQueue<Task, BlockSize>  (unbounded MPMC, default)
//             BoundedMPMCQueue<Task, Size>         (bounded MPMC)
//             SPSCQueue<Task, Size>                (bounded, 1P1C)
//
//   The queue must have:
//     - using value_type = Task    (callable type)
//     - bool try_push(Task&&)      (returns false if full for bounded queues)
//     - bool try_pop(Task&)        (returns false if empty)
//
//   All project queue types satisfy these requirements.
//
// ── Usage ─────────────────────────────────────────────────────────────
//     MPMCEventLoop<> loop;
//     loop.start();
//     loop.post([] { do_something(); });
//     loop.post_batch(begin, end);
//     loop.post_and_wait([&] { result = compute(); });
//     loop.stop();
//
// ── Re-entrancy ───────────────────────────────────────────────────────
//   post_and_wait() detects whether the caller is the event-loop thread
//   itself and executes inline to avoid deadlock.
//
// ── Exception safety ──────────────────────────────────────────────────
//   If a task throws, the exception propagates through the worker thread
//   and calls std::terminate.  Producers should ensure their tasks are
//   noexcept or catch internally.

template <typename Queue>
class EventLoop {
public:
    using Task = typename Queue::value_type;
    static_assert(std::is_invocable_v<Task>,
                  "Queue::value_type must be callable with no arguments");

public:
    EventLoop() = default;

    ~EventLoop() noexcept { stop(); }

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // ─── Lifecycle ─────────────────────────────────────────────────────

    /// Start the consumer thread.  Idempotent — safe to call multiple times.
    void start() {
        if (_running.exchange(true, std::memory_order_acq_rel))
            return;
        _worker  = std::jthread([this](std::stop_token st) { _run(st); });
        _tid     = _worker.get_id();
    }

    /// Request graceful shutdown.  The worker drains tasks already in the
    /// queue before exiting.  Tasks posted concurrently with stop() may be
    /// silently dropped (the queue destructor destroys them safely).
    /// Idempotent.
    void stop() noexcept {
        if (!_running.exchange(false, std::memory_order_acq_rel))
            return;
        _worker.request_stop();
        _wake.fetch_add(1, std::memory_order_release);
        _wake.notify_one();
        if (_worker.joinable())
            _worker.join();
        _tid = std::thread::id{};
    }

    // ─── Task submission (producer side) ───────────────────────────────

    /// Post a single task.  Returns false only when using a bounded queue
    /// that is full (task silently dropped).  Unbounded queues always
    /// return true.
    template <typename F>
    bool post(F&& task) {
        if (!_queue.try_push(std::forward<F>(task)))
            return false;
        _signal();
        return true;
    }

    /// Post a batch of tasks, waking the consumer only once after all
    /// pushes.  Returns the number of tasks enqueued (may be < the batch
    /// size for a full bounded queue).
    template <typename Iter>
    size_t post_batch(Iter begin, Iter end) {
        size_t count = 0;
        for (; begin != end; ++begin) {
            if (_queue.try_push(std::move(*begin)))
                ++count;
            else
                break;              // bounded queue full
        }
        if (count > 0) {
            _wake.fetch_add(static_cast<uint64_t>(count), std::memory_order_release);
            _wake.notify_one();
        }
        return count;
    }

    /// Post a task and block the caller until it completes.
    ///
    /// Re-entrancy: if the caller *is* the event-loop thread, the task is
    /// executed inline (no deadlock).  If the loop hasn't been started yet
    /// the task is also executed inline.
    template <typename F>
    void post_and_wait(F&& task) {
        if (!_running.load(std::memory_order_acquire) ||
            std::this_thread::get_id() == _tid.load(std::memory_order_acquire)) {
            std::forward<F>(task)();
            return;
        }

        auto prom = std::make_shared<std::promise<void>>();
        auto fut  = prom->get_future();

        // Pre-wrap in std::function<Task> so that the move/copy of a
        // move-only callable happens BEFORE we touch the bounded queue.
        // If we let try_push() do implicit conversion from the raw lambda,
        // the lambda would be consumed on EVERY conversion attempt, even
        // when the queue is full — making retry impossible.
        auto wrapped = Task([t = std::forward<F>(task),
                                            prom]() mutable {
            t();
            prom->set_value();
        });
        // Move the wrapped task into the queue.  For move-only types
        // (e.g. std::packaged_task) the queue accepts only rvalues; for
        // copyable types this also avoids an extra copy.
        // If the bounded queue is full, try_push returns false BEFORE
        // the placement new (std::move is just a cast, no actual move),
        // so wrapped remains valid for retry.
        while (!post(std::move(wrapped))) {
            std::this_thread::yield();
        }

        fut.wait();
    }

    /// Non-blocking pop from the consumer side.  Useful for draining
    /// without starting the dedicated loop thread.
    bool try_pop(Task& out) noexcept { return _queue.try_pop(out); }

    // ─── Queries ───────────────────────────────────────────────────────

    /// Approximate number of pending tasks (point-in-time for MPMC, exact
    /// for SPSC).
    size_t pending() const noexcept { return _queue.size(); }

    bool empty()      const noexcept { return _queue.empty(); }
    bool is_running() const noexcept { return _running.load(std::memory_order_acquire); }

private:
    void _signal() noexcept {
        _wake.fetch_add(1, std::memory_order_release);
        _wake.notify_one();
    }

    void _run(std::stop_token st) {
        while (!st.stop_requested()) {
            // ── Drain ALL available tasks before checking stop ──
            Task task;
            while (_queue.try_pop(task)) {
                task();
            }

            // ── Queue empty — safe to check for stop ──
            if (st.stop_requested())
                return;

            // ── Double-check: load _wake, then re-check the queue ──
            auto prev = _wake.load(std::memory_order_acquire);
            if (_queue.try_pop(task)) {
                task();
                continue;
            }

            // ── Block until a producer increments _wake ──
            _wake.wait(prev, std::memory_order_acquire);
        }
    }

    Queue                     _queue;
    std::atomic<uint64_t>     _wake{0};
    std::atomic<bool>         _running{false};
    std::atomic<std::thread::id> _tid{};
    std::jthread              _worker;
};


// ─── Convenience aliases ──────────────────────────────────────────────────

/// Default — unbounded MPMC event loop.
template <typename Task = std::function<void()>>
using MPMCEventLoop = EventLoop<SegmentedMPMCQueue<Task, 1024>>;

/// Bounded MPMC event loop with explicit capacity.
template <typename Task = std::function<void()>, size_t N = 256>
using BoundedEventLoop = EventLoop<BoundedMPMCQueue<Task, N>>;

/// Single-producer, single-consumer event loop (fastest for 1P1C).
template <typename Task = std::function<void()>, size_t N = 64>
using SPSCEventLoop = EventLoop<SPSCQueue<Task, N>>;
