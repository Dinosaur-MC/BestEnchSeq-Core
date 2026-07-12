#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

#include "BoundedMPMCQueue.hpp"
#include "SegmentedMPMCQueue.hpp"
#include "SPSCQueue.hpp"

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
//     - bool push(Task&&)          (or void — see kPushReturnsBool)
//     - bool pop(Task&)            (or try_pop — detected at compile time)
//
//   Use the convenience aliases at the bottom for common combinations.
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

private:
    // ── API normalisation helpers ─────────────────────────────────────

    // Detect whether Queue::push returns bool (bounded, may drop) or void
    // (unbounded / overwrite — never fails).
    static constexpr bool kPushReturnsBool =
        std::is_same_v<decltype(std::declval<Queue>().push(std::declval<Task>())), bool>;

    // Normalise pop → try_pop: SPSCQueue / BoundedMPMCQueue use pop(),
    // while SegmentedMPMCQueue uses try_pop().  Both have the same
    // signature bool(T&).
    template <typename Q, typename T>
    static bool consume(Q& q, T& out) noexcept {
        if constexpr (requires { q.try_pop(out); })
            return q.try_pop(out);
        else
            return q.pop(out);
    }

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
        if constexpr (kPushReturnsBool) {
            if (!_queue.push(std::forward<F>(task)))
                return false;
        } else {
            _queue.push(std::forward<F>(task));
        }
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
            if constexpr (kPushReturnsBool) {
                if (_queue.push(std::move(*begin)))
                    ++count;
                else
                    break;              // bounded queue full
            } else {
                _queue.push(std::move(*begin));
                ++count;
            }
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

        post([t = std::forward<F>(task), p = std::move(prom)]() mutable {
            t();
            p->set_value();
        });

        fut.wait();
    }

    /// Non-blocking pop from the consumer side.  Useful for draining
    /// without starting the dedicated loop thread.
    bool try_pop(Task& out) noexcept { return consume(_queue, out); }

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
        uint64_t last_wake = _wake.load(std::memory_order_acquire);

        while (!st.stop_requested()) {
            // ── Drain all currently available tasks ──
            Task task;
            while (consume(_queue, task)) {
                if (st.stop_requested())
                    return;             // honour stop even mid-drain
                task();
            }

            // ── Empty: wait until a producer increments _wake ──
            // This is a blocking wait (futex on Linux, WaitOnAddress on
            // Windows).  Zero CPU while blocked.
            _wake.wait(last_wake, std::memory_order_acquire);
            last_wake = _wake.load(std::memory_order_acquire);
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
