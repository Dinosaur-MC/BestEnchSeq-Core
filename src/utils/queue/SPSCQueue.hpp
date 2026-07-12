#pragma once
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

// ─── SPSCQueue ───
// Single-Producer, Single-Consumer lock-free bounded queue.
//
// See also: BoundedMPMCQueue (N readers + N writers),
//           SegmentedMPMCQueue (unbounded MPMC),
//           SPMCQueue (1 writer + N readers).
// Design doc: docs/MPMCQueue.md
//
// Fixed capacity. When full, push() silently drops the new value.
// No data races: producer and consumer never touch the same atomic.
//
// Thread safety:
//   - One writer: push()
//   - One reader: pop()
//   - No mutexes, no CAS loops

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
    using value_type = T;

    SPSCQueue() = default;

    ~SPSCQueue() noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ─── Producer ───
    // Push a value. Returns false if the queue is full (no overwrite).
    template <typename U>
    bool push(U&& value) {
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);

        if (w - r >= Capacity)
            return false;  // full, silently drop

        T* slot = std::launder(reinterpret_cast<T*>(
            &_buffer[(w % Capacity) * sizeof(T)]));
        ::new (slot) T(std::forward<U>(value));
        _write.value.store(w + 1, std::memory_order_release);
        return true;
    }

    // ─── Consumer ───
    // Read one item into `out`. Returns false if empty.
    bool pop(T& out) noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);

        if (r == w)
            return false;

        T* item = std::launder(reinterpret_cast<T*>(
            &_buffer[(r % Capacity) * sizeof(T)]));
        out = std::move(*item);
        item->~T();
        _read.value.store(r + 1, std::memory_order_release);
        return true;
    }

    // Peek without consuming (const, consumer only).
    bool peek(T& out) const noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        out = *std::launder(reinterpret_cast<const T*>(
            &_buffer[(r % Capacity) * sizeof(T)]));
        return true;
    }

    size_t size() const noexcept {
        size_t w = _write.value.load(std::memory_order_acquire);
        size_t r = _read.value.load(std::memory_order_relaxed);
        return w > r ? w - r : 0;
    }

    bool empty() const noexcept { return size() == 0; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    // Producer cache line
    alignas(CL) AlignedAtomic _write;

    // Consumer cache line
    alignas(CL) AlignedAtomic _read;

    // Storage
    alignas(T) std::byte _buffer[Capacity * sizeof(T)];
};
