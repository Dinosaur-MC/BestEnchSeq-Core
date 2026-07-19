#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <iterator>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

// ─── Portable [[no_unique_address]] ──────────────────────────────────────
// Clang in MSVC-compatibility mode (Windows) warns on the C++20 standard
// [[no_unique_address]] but supports [[msvc::no_unique_address]] silently.
// Other compilers (GCC, Clang on Linux/macOS) use the standard spelling.
#if defined(__clang__) && defined(_MSC_VER)
#  define BESQ_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address) >= 201803L
#  define BESQ_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#  define BESQ_NO_UNIQUE_ADDRESS
#endif

#include "queue/BoundedMPMCQueue.hpp"
#include "queue/BoundedMPSCQueue.hpp"
#include "queue/SegmentedMPSCQueue.hpp"
#include "queue/SegmentedMPMCQueue.hpp"
#include "queue/SPSCQueue.hpp"

// ─── EventLoop ────────────────────────────────────────────────────────────
// Zero-CPU-idle task/data event loop built on C++20 atomic wait/notify.
//
// A single consumer thread processes items submitted from any number of
// producer threads.  When the queue is empty the consumer blocks in
// std::atomic<uint64_t>::wait — zero CPU, zero polling, immediate wake
// on the next post().
//
// ── Template parameters ───────────────────────────────────────────────
//   T         — queue element type.
//               Callable mode: T must be invocable (e.g. std::function<void()>)
//               Data mode:     T is any movable data type
//   Queue     — a lock-free queue satisfying QueueType<T>:
//                 SegmentedMPMCQueue<T, BlockSize>  (unbounded MPMC, default)
//                 BoundedMPMCQueue<T, Size>         (bounded MPMC)
//                 SPSCQueue<T, Size>                (bounded, 1P1C)
//                 SegmentedMPSCQueue<T>             (unbounded MPSC)
//   Handler   — void (default) = callable mode; the consumer thread invokes
//               each T directly.
//               Otherwise = data mode; must be invocable as void(T).
//               The Handler is stored inside EventLoop with [[no_unique_address]]
//               so non-void Handlers add zero overhead.
//
// ── Usage ─────────────────────────────────────────────────────────────
//     // Callable mode (Handler = void)
//     MPMCEventLoop<> loop;
//     loop.start();
//     loop.post([] { do_something(); });
//     loop.try_post([] { maybe_dropped(); });
//     loop.stop();
//
//     // Data mode (explicit Handler)
//     struct MyHandler {
//         void operator()(std::string s) { process(std::move(s)); }
//     };
//     EventLoop<std::string, SegmentedMPMCQueue<std::string, 1024>, MyHandler>
//         loop{MyHandler{}};
//     loop.post("hello");
//
// ── Exception safety ──────────────────────────────────────────────────
//   If a task or handler throws, the exception propagates through the
//   worker thread and calls std::terminate.  Producers should ensure their
//   tasks/handlers are noexcept or catch internally.

template <typename T, typename Queue, typename Handler = void>
    requires ((std::is_void_v<Handler> && std::invocable<T>) ||
              (!std::is_void_v<Handler> && std::invocable<Handler, T>))
class EventLoop {
public:
    // ─── Lifecycle ─────────────────────────────────────────────────────

    /// Default constructor (callable mode only).
    EventLoop() requires std::is_void_v<Handler> = default;

    /// Construct with a Handler instance (data mode only).
    template <typename H>
        requires (!std::is_void_v<Handler> && std::same_as<std::remove_cvref_t<H>, Handler>)
    explicit EventLoop(H&& handler)
        : _handler(std::forward<H>(handler)) {}

    ~EventLoop() noexcept { stop(); }

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// Start the consumer thread.  Idempotent — safe to call multiple times.
    void start() {
        if (_running.exchange(true, std::memory_order_acq_rel))
            return;
        _worker = std::jthread([this](std::stop_token st) { _run(st); });
    }

    /// Request the consumer thread to stop.
    /// @param force  If true, discard remaining queued items after the
    ///               worker exits (force shutdown).  If false (default),
    ///               the worker drains the queue before exiting (graceful).
    /// Idempotent.
    void stop(bool force = false) noexcept {
        if (!_running.exchange(false, std::memory_order_acq_rel))
            return;

        _worker.request_stop();
        _wake.fetch_add(1, std::memory_order_release);
        _wake.notify_one();

        if (_worker.joinable())
            _worker.join();

        if (force) {
            T discard;
            while (_queue.try_pop(discard)) {}
        }
    }

    // ─── Submission (producer side) ────────────────────────────────────

    /// Attempt to enqueue a single item.  Returns false only when using a
    /// bounded queue that is full (item silently dropped).  Unbounded
    /// queues always return true.
    template <typename U>
        requires (std::is_void_v<Handler>
                  ? std::convertible_to<U&&, T>
                  : std::same_as<std::remove_cvref_t<U>, T>)
    bool try_post(U&& item) {
        if (!_queue.try_push(std::forward<U>(item)))
            return false;
        _signal();
        return true;
    }

    /// Block until the item is enqueued.  For bounded queues this spins
    /// with yield(); unbounded queues succeed on the first attempt.
    template <typename U>
        requires (std::is_void_v<Handler>
                  ? std::convertible_to<U&&, T>
                  : std::same_as<std::remove_cvref_t<U>, T>)
    void post(U&& item) {
        while (!try_post(std::forward<U>(item)))
            std::this_thread::yield();
    }

    /// Attempt to emplace a T in-place from the given arguments.  Returns
    /// false only when using a bounded queue that is full (item silently
    /// dropped).  Unbounded queues always return true.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_post_emplace(Args&&... args) {
        if (!_queue.try_emplace(std::forward<Args>(args)...))
            return false;
        _signal();
        return true;
    }

    /// Block until the T is emplaced in-place from the given arguments.
    /// For bounded queues this spins with yield(); unbounded queues
    /// succeed on the first attempt.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    void post_emplace(Args&&... args) {
        while (!try_post_emplace(std::forward<Args>(args)...))
            std::this_thread::yield();
    }

    /// Best-effort batch enqueue.  Returns the number of items enqueued
    /// (may be less than the range size for a full bounded queue).
    template <std::input_iterator Iter>
        requires (std::is_void_v<Handler>
                  ? std::convertible_to<typename std::iter_value_t<Iter>, T>
                  : std::same_as<typename std::iter_value_t<Iter>, T>)
    size_t try_post_batch(Iter begin, Iter end) {
        size_t count = 0;
        for (; begin != end; ++begin) {
            if (_queue.try_push(std::move(*begin)))
                ++count;
            else
                break;
        }
        if (count > 0) {
            _wake.fetch_add(static_cast<uint64_t>(count), std::memory_order_release);
            _wake.notify_one();
        }
        return count;
    }

    /// Block until all items in the range are enqueued.
    template <std::input_iterator Iter>
        requires (std::is_void_v<Handler>
                  ? std::convertible_to<typename std::iter_value_t<Iter>, T>
                  : std::same_as<typename std::iter_value_t<Iter>, T>)
    void post_batch(Iter begin, Iter end) {
        for (; begin != end; ++begin)
            post(std::move(*begin));
    }

    // ─── Queries ───────────────────────────────────────────────────────

    /// Approximate number of pending items (point-in-time for MPMC, exact
    /// for SPSC).
    [[nodiscard]] size_t pending() const noexcept { return _queue.size(); }

    [[nodiscard]] bool empty()      const noexcept { return _queue.empty(); }
    [[nodiscard]] bool is_running() const noexcept { return _running.load(std::memory_order_acquire); }

private:
    /// Tag type used as handler storage when Handler is void.
    struct EmptyHandler {};

    /// Storage type: maps Handler=void to empty tag, otherwise keeps Handler.
    using HandlerStorage = std::conditional_t<std::is_void_v<Handler>, EmptyHandler, Handler>;

    void _signal() noexcept {
        _wake.fetch_add(1, std::memory_order_release);
        _wake.notify_one();
    }

    void _run(std::stop_token st) {
        while (!st.stop_requested()) {
            // ── Drain ALL available items before checking stop ──
            T item;
            while (_queue.try_pop(item)) {
                _dispatch(item);
            }

            // ── Queue empty — safe to check for stop ──
            if (st.stop_requested())
                return;

            // ── Double-check: load _wake, then re-check the queue ──
            auto prev = _wake.load(std::memory_order_acquire);
            if (_queue.try_pop(item)) {
                _dispatch(item);
                continue;
            }

            // ── Block until a producer increments _wake ──
            _wake.wait(prev, std::memory_order_acquire);
        }
    }

    void _dispatch(T& item) noexcept {
        if constexpr (std::is_void_v<Handler>)
            std::invoke(std::move(item));
        else
            std::invoke(_handler, std::move(item));
    }

    Queue                       _queue;
    std::atomic<uint64_t>       _wake{0};
    std::atomic<bool>           _running{false};
    std::jthread                _worker;
    BESQ_NO_UNIQUE_ADDRESS HandlerStorage _handler{};
};


// ─── Convenience aliases (callable mode only) ─────────────────────────────

/// Default — unbounded MPMC event loop.
template <typename Task = std::function<void()>>
using MPMCEventLoop = EventLoop<Task, SegmentedMPMCQueue<Task, 1024>>;

/// Bounded MPMC event loop with explicit capacity.
template <typename Task = std::function<void()>, size_t N = 256>
using BoundedEventLoop = EventLoop<Task, BoundedMPMCQueue<Task, N>>;

/// Single-producer, single-consumer event loop (fastest for 1P1C).
template <typename Task = std::function<void()>, size_t N = 64>
using SPSCEventLoop = EventLoop<Task, SPSCQueue<Task, N>>;

/// Multi-producer, single-consumer event loop (unbounded, zero-allocation hot path).
template <typename Task = std::function<void()>>
using MPSCEventLoop = EventLoop<Task, SegmentedMPSCQueue<Task>>;

/// Bounded multi-producer, single-consumer event loop (ring-buffer, fast).
template <typename Task = std::function<void()>, size_t N = 256>
using BoundedMPSCEventLoop = EventLoop<Task, BoundedMPSCQueue<Task, N>>;
