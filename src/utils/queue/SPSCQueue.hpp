#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

// ─── SPSCQueue ───
// Single-Producer, Single-Consumer lock-free bounded queue.
//
// Thread safety:
//   - One writer: push() / try_push()
//   - One reader: pop() / try_pop()
//   - No mutexes, no CAS loops
//
// Features:
//   - FIFO order guaranteed
//   - Fixed capacity; when full, push() silently drops the value
//   - Peek without consuming via front()
//
// STL-style type aliases:
//   value_type, reference, const_reference, size_type, difference_type

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");

    static constexpr size_t CL = 64;

    struct alignas(CL) AlignedAtomic {
        std::atomic<size_t> value{0};
    };

private:
    T* ptr(size_t idx) noexcept {
        return std::launder(reinterpret_cast<T*>(
            &_buffer[(idx % Capacity) * sizeof(T)]));
    }
    const T* ptr(size_t idx) const noexcept {
        return std::launder(reinterpret_cast<const T*>(
            &_buffer[(idx % Capacity) * sizeof(T)]));
    }

public:
    // ─── STL style type aliases ───────────────────────────────────────
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction / destruction ───────────────────────────────────

    SPSCQueue() = default;

    ~SPSCQueue() noexcept {
        size_type r = _read.value.load(std::memory_order_relaxed);
        size_type w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a value.  Returns false if the queue is full (silently drops).
    /// Thread-safe for a single producer thread.
    template <typename U>
    bool push(U&& value) noexcept {
        size_type w = _write.value.load(std::memory_order_relaxed);
        size_type r = _read.value.load(std::memory_order_acquire);

        if (w - r >= Capacity)
            return false;

        T* slot = ptr(w);
        ::new (slot) T(std::forward<U>(value));
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    /// Non-blocking push.  Alias for push() (same semantics).
    template <typename U>
    bool try_push(U&& value) noexcept { return push(std::forward<U>(value)); }

    // ─── Consumer API ─────────────────────────────────────────────────

    /// Pop the oldest element into `out`.  Returns false if empty.
    /// Thread-safe for a single consumer thread.
    bool pop(value_type& out) noexcept {
        size_type r = _read.value.load(std::memory_order_relaxed);
        size_type w = _write.value.load(std::memory_order_acquire);

        if (r == w)
            return false;

        T* item = ptr(r);
        out = std::move(*item);
        item->~T();
        _read.value.store(r + 1, std::memory_order_release);
        return true;
    }

    /// Non-blocking pop.  Alias for pop() (same semantics).
    bool try_pop(value_type& out) noexcept { return pop(out); }

    /// Peek at the oldest element without consuming it.
    /// Returns false if empty.  Consumer-only.
    bool front(value_type& out) const noexcept { return peek(out); }

    /// Peek (legacy name, same as front()).
    bool peek(value_type& out) const noexcept {
        size_type r = _read.value.load(std::memory_order_relaxed);
        size_type w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        out = *std::launder(reinterpret_cast<const T*>(
            &_buffer[(r % Capacity) * sizeof(T)]));
        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    /// Number of elements currently in the queue (exact for SPSC).
    size_type size() const noexcept {
        size_type w = _write.value.load(std::memory_order_acquire);
        size_type r = _read.value.load(std::memory_order_relaxed);
        return w > r ? w - r : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    static constexpr size_type capacity() noexcept { return Capacity; }

    /// Remove all elements.  NOT thread-safe — caller must ensure no
    /// concurrent push or pop.
    void clear() noexcept {
        size_type r = _read.value.load(std::memory_order_relaxed);
        size_type w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
        _read.value.store(w, std::memory_order_release);
    }

private:
    // Producer cache line
    alignas(CL) AlignedAtomic _write;

    // Consumer cache line
    alignas(CL) AlignedAtomic _read;

    // Storage
    alignas(T) std::byte _buffer[Capacity * sizeof(T)];
};
