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

template <typename T, size_t Capacity>
class SPSCQueue final : public IQueue<T> {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");

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
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction ─────────────────────────────────────────────────

    SPSCQueue() = default;

    ~SPSCQueue() noexcept final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) { ptr(r)->~T(); ++r; }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a copy.  Returns false if the queue is full.
    bool try_push(const T& value) noexcept override final {
        if constexpr (!std::is_copy_constructible_v<T>)
            return false;
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        ::new (ptr(w)) T(value);
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    /// Push by move.  Returns false if the queue is full.
    bool try_push(T&& value) noexcept override final {
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        ::new (ptr(w)) T(std::move(value));
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    /// Emplace a T in-place from constructor arguments.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_emplace(Args&&... args) noexcept {
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        ::new (ptr(w)) T(std::forward<Args>(args)...);
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    // ─── Consumer API ─────────────────────────────────────────────────

    bool try_pop(T& out) noexcept override final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        T* item = ptr(r);
        out = std::move(*item);
        item->~T();
        _read.value.store(r + 1, std::memory_order_release);
        return true;
    }

    /// Peek without consuming.
    bool front(T& out) const noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        out = *std::launder(reinterpret_cast<const T*>(
            &_buffer[(r % Capacity) * sizeof(T)]));
        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    size_t size() const noexcept override final {
        size_t w = _write.value.load(std::memory_order_acquire);
        size_t r = _read.value.load(std::memory_order_relaxed);
        return w > r ? w - r : 0;
    }

    bool empty() const noexcept override final { return size() == 0; }

    size_t capacity() const noexcept override final { return Capacity; }

    void clear() noexcept override final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) { ptr(r)->~T(); ++r; }
        _read.value.store(w, std::memory_order_release);
    }

private:
    alignas(CL) AlignedAtomic _write;
    alignas(CL) AlignedAtomic _read;
    alignas(T) std::byte _buffer[Capacity * sizeof(T)];
};
