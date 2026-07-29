#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

// ─── BESQ_PAUSE ────────────────────────────────────────────────────────────
// CPU pause / yield hint for spin-loop body.  On x86 emits the PAUSE
// instruction (improving SMT performance and reducing power).  On ARM
// emits YIELD.  Falls back to empty on unknown architectures.
//
// Use inside ultra-short spin loops where `std::this_thread::yield()` (a
// syscall on most OSes) would be too heavy.  For longer spins where
// fairness matters, prefer yield().

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#  include <intrin.h>
#  define BESQ_PAUSE() _mm_pause()
#elif defined(__x86_64__) || defined(__i386__)
#  define BESQ_PAUSE() __asm__ volatile("pause" ::: "memory")
#elif defined(__aarch64__)
#  define BESQ_PAUSE() __asm__ volatile("yield" ::: "memory")
#else
#  define BESQ_PAUSE() /* no pause hint */
#endif

// ─── IQueue<T> (virtual interface) ────────────────────────────────────────
// Runtime-polymorphic wrapper for lock-free queues.
//
// Use when queue type must be selected at runtime (e.g. config-driven
// backend choice).  For performance-critical paths prefer the concrete
// queue type directly — the vtable dispatch adds ~2-4 ns per call.
//
// try_emplace is available as a non-virtual template on the interface
// (default: constructs T and forwards to try_push).  Concrete queues
// may override with a direct-into-slot implementation for higher
// efficiency.
//
// All implementations must be thread-safe as documented by the concrete type.

template <typename T>
class IQueue {
public:
    // ─── STL style type aliases ───────────────────────────────────────
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    virtual ~IQueue() = default;

    /// Push a copy of `item`.  Returns false if the queue is full and
    /// the item was dropped.  Unbounded queues always return true.
    virtual bool try_push(const T& item) = 0;

    /// Move-construct from `item`.  Returns false if full (drop).
    virtual bool try_push(T&& item) = 0;

    /// Pop the oldest element into `item`.  Returns false if empty.
    virtual bool try_pop(T& item) = 0;

    /// Approximate number of elements (point-in-time for MPMC).
    virtual size_t size() const = 0;

    /// True if the queue contains no elements.
    virtual bool empty() const = 0;

    /// Maximum capacity (0 = unbounded / effectively unlimited).
    virtual size_t capacity() const = 0;

    /// Remove all elements.  Default implementation drains via try_pop().
    /// For non-default-constructible T, concrete queue must override.
    virtual void clear() {
        if constexpr (std::is_default_constructible_v<T>) {
            T item;
            while (try_pop(item)) {}
        }
    }

    /// Emplace a T in-place from constructor arguments.
    /// Non-virtual template: dispatches to try_push by default.
    /// Concrete queues may provide a more efficient implementation
    /// (e.g. BoundedMPMCQueue, BoundedMPSCQueue) that constructs
    /// directly into the slot.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_emplace(Args&&... args) {
        return try_push(T(std::forward<Args>(args)...));
    }
};


// ─── Queue concept (C++20, compile-time) ──────────────────────────────────
// Syntactic contract for all project queue types.
// Every concrete queue (BoundedMPMCQueue, SegmentedMPMCQueue, SPSCQueue)
// satisfies this concept.

template <typename Q, typename T>
concept QueueType = requires(Q& q, const T& cval, T& val) {
    { q.try_push(cval) }             -> std::same_as<bool>;
    { q.try_push(std::move(val)) }   -> std::same_as<bool>;
    { q.try_pop(val) }               -> std::same_as<bool>;
    { q.size() }                     -> std::convertible_to<size_t>;
    { q.empty() }                    -> std::convertible_to<bool>;
};


// ─── QueueAdaptor<T, ConcreteQueue> ───────────────────────────────────────
// Wraps a concrete lock-free queue behind the IQueue<T> virtual interface.
//
// Example:
//   QueueAdaptor<int, SPSCQueue<int, 256>> adapted;
//   IQueue<int>& q = adapted;          // runtime-polymorphic access
//   q.try_push(42);                    // virtual dispatch
//
// The adaptor stores the concrete queue inline (no heap allocation).

template <typename T, QueueType<T> ConcreteQueue>
class QueueAdaptor final : public IQueue<T> {
public:
    template <typename... Args>
    explicit QueueAdaptor(Args&&... args)
        : _queue(std::forward<Args>(args)...) {}

    bool try_push(const T& item) override {
        if constexpr (std::is_copy_constructible_v<T>)
            return _queue.try_push(item);
        else
            return false;
    }

    bool try_push(T&& item) override {
        return _queue.try_push(std::move(item));
    }

    bool try_pop(T& item) override {
        return _queue.try_pop(item);
    }

    size_t size() const override {
        return _queue.size();
    }

    bool empty() const override {
        return _queue.empty();
    }

    size_t capacity() const override {
        return _queue.capacity();
    }

    /// Access the underlying concrete queue (unwrapped).
    ConcreteQueue& underlying() noexcept { return _queue; }
    const ConcreteQueue& underlying() const noexcept { return _queue; }

private:
    ConcreteQueue _queue;
};

