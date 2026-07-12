#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

// ─── SPSCQueue ───
// Single-Producer, Single-Consumer lock-free bounded queue.
//
// Inherits from IQueue<T> for runtime-polymorphic access.
// Use directly (SPSCQueue<int,64> q) for zero-overhead inline calls;
// use through IQueue<int>& for virtual dispatch when needed.
//
// Thread safety:
//   - One writer: try_push()
//   - One reader: try_pop() / front()
//   - No mutexes, no CAS loops
//
// Features:
//   - FIFO order guaranteed
//   - Fixed capacity; when full, try_push() returns false (drops)
//   - Peek without consuming via front()

template <typename T, size_t Capacity>
class SPSCQueue final : public IQueue<T> {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");

    using Base = IQueue<T>;
    static constexpr size_t CL = 64;

    struct alignas(CL) AlignedAtomic {
        std::atomic<size_t> value{0};
    };

    T* ptr(size_t idx) noexcept {
        return std::launder(reinterpret_cast<T*>(
            &_buffer[(idx % Capacity) * sizeof(T)]));
    }
    const T* ptr(size_t idx) const noexcept {
        return std::launder(reinterpret_cast<const T*>(
            &_buffer[(idx % Capacity) * sizeof(T)]));
    }

public:
    // ─── STL style type aliases (repeated for non-polymorphic use) ───
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction / destruction ───────────────────────────────────

    SPSCQueue() = default;

    ~SPSCQueue() noexcept final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a copy.  Returns false if the queue is full.
    /// Not available for move-only T (copy constructor required).
    bool try_push(const T& value) noexcept override {
        if constexpr (std::is_copy_constructible_v<T>)
            return try_push_impl(value);
        else
            return false;
    }

    /// Push by move.  Returns false if the queue is full.
    bool try_push(T&& value) noexcept override {
        return try_push_impl(std::move(value));
    }

    // ─── Consumer API ─────────────────────────────────────────────────

    /// Pop the oldest element.  Returns false if empty.
    bool try_pop(T& out) noexcept override {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);

        if (r == w)
            return false;

        T* item = ptr(r);
        out = std::move(*item);
        item->~T();
        _read.value.store(r + 1, std::memory_order_release);
        return true;
    }

    /// Peek at the oldest element without consuming it.
    bool front(T& out) const noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        out = *std::launder(reinterpret_cast<const T*>(
            &_buffer[(r % Capacity) * sizeof(T)]));
        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    size_t size() const noexcept override {
        size_t w = _write.value.load(std::memory_order_acquire);
        size_t r = _read.value.load(std::memory_order_relaxed);
        return w > r ? w - r : 0;
    }

    bool empty() const noexcept override { return size() == 0; }

    size_t capacity() const noexcept override { return Capacity; }

    void clear() noexcept override {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
        _read.value.store(w, std::memory_order_release);
    }

private:
    template <typename U>
    bool try_push_impl(U&& value) {
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);

        if (w - r >= Capacity)
            return false;

        T* slot = ptr(w);
        ::new (slot) T(std::forward<U>(value));
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    // Producer cache line
    alignas(CL) AlignedAtomic _write;

    // Consumer cache line
    alignas(CL) AlignedAtomic _read;

    // Storage
    alignas(T) std::byte _buffer[Capacity * sizeof(T)];
};
