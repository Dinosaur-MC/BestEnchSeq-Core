#pragma once

#include <cstddef>

// ─── IQueue<T> (virtual interface) ────────────────────────────────────────
// Runtime-polymorphic wrapper for lock-free queues.
//
// Use when queue type must be selected at runtime (e.g. config-driven
// backend choice).  For performance-critical paths prefer the concrete
// queue type directly — the vtable dispatch adds ~2-4 ns per call.
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
    virtual void clear() {
        T item;
        while (try_pop(item)) {}
    }
};


// ─── Queue concept (C++20, compile-time) ──────────────────────────────────
// Documents the syntactic contract expected by generic algorithms.
//
// Checks are illustrative — the concept is intentionally permissive
// (push may return void or bool, pop may be named try_pop or pop).
// Use it as a documentation aid; real constraints depend on the algorithm.

#ifdef __cpp_concepts

#include <concepts>

template <typename Q, typename T>
concept QueueType = requires(Q& q, const T& cval, T& val) {
    { q.push(cval) };
    { q.try_pop(val) } -> std::convertible_to<bool>;
    { q.size() }       -> std::convertible_to<size_t>;
    { q.empty() }      -> std::convertible_to<bool>;
};

#endif // __cpp_concepts


// ─── QueueAdaptor<T, ConcreteQueue> ───────────────────────────────────────
// Wraps a concrete lock-free queue behind the IQueue<T> virtual interface.
//
// Example:
//   QueueAdaptor<int, SPSCQueue<int, 256>> adapted;
//   IQueue<int>& q = adapted;          // runtime-polymorphic access
//   q.try_push(42);                    // virtual dispatch
//
// The adaptor stores the concrete queue inline (no heap allocation).

template <typename T, typename ConcreteQueue>
class QueueAdaptor final : public IQueue<T> {
public:
    template <typename... Args>
    explicit QueueAdaptor(Args&&... args)
        : _queue(std::forward<Args>(args)...) {}

    bool try_push(const T& item) override {
        return _queue.push(item);
    }

    bool try_push(T&& item) override {
        return _queue.push(std::move(item));
    }

    bool try_pop(T& item) override {
        // Normalise pop → try_pop (see EventLoop::consume for rationale)
        if constexpr (requires { _queue.try_pop(item); })
            return _queue.try_pop(item);
        else
            return _queue.pop(item);
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

