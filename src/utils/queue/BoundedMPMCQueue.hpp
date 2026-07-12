#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

// ─── BoundedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free bounded queue.
// Algorithm: Dmitry Vyukov (sequence-number ring buffer).
//   https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
//
// Inherits from IQueue<T> for runtime-polymorphic access.
// Requirements: T nothrow-destructible and nothrow-move-assignable,
//               Capacity power of two ≥ 2.

template <typename T, size_t Capacity>
class BoundedMPMCQueue final : public IQueue<T> {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

    static constexpr size_t CL = 64;

    struct alignas(CL) AlignedAtomic {
        std::atomic<size_t> value{0};
    };

    struct Slot {
        std::atomic<uint64_t> sequence;
        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(_storage)); }
        const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(_storage)); }
    private:
        alignas(T) unsigned char _storage[sizeof(T)];
    };

    // ─── Push implementation (shared by both overloads) ──────────────
    // Inlined into each override to avoid a function-call layer.
    // U = const T& for copy, U = T&& for move.

    template <typename U>
    bool push_impl(U&& value) noexcept {
        size_t pos = _write.value.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);
            if (diff == 0) {
                if (_write.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    ::new (slot.ptr()) T(std::forward<U>(value));
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = _write.value.load(std::memory_order_relaxed);
            }
        }
    }

public:
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    BoundedMPMCQueue() noexcept {
        for (size_t i = 0; i < Capacity; ++i)
            _slots[i].sequence.store(i, std::memory_order_relaxed);
    }

    ~BoundedMPMCQueue() noexcept final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_relaxed);
        while (r != w) { _slots[r & _mask].ptr()->~T(); ++r; }
    }

    BoundedMPMCQueue(const BoundedMPMCQueue&) = delete;
    BoundedMPMCQueue& operator=(const BoundedMPMCQueue&) = delete;

    // ─── Producer ─────────────────────────────────────────────────────

    bool try_push(const T& value) noexcept override final {
        if constexpr (!std::is_copy_constructible_v<T>)
            return false;
        return push_impl(value);
    }

    bool try_push(T&& value) noexcept override final {
        return push_impl(std::move(value));
    }

    // ─── Consumer ─────────────────────────────────────────────────────

    bool try_pop(T& out) noexcept override final {
        size_t pos = _read.value.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos + 1);
            if (diff == 0) {
                if (_read.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(*slot.ptr());
                    slot.ptr()->~T();
                    slot.sequence.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = _read.value.load(std::memory_order_relaxed);
            }
        }
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
        size_t w = _write.value.load(std::memory_order_relaxed);
        while (r != w) { _slots[r & _mask].ptr()->~T(); ++r; }
        _read.value.store(w, std::memory_order_release);
    }

private:
    alignas(CL) AlignedAtomic _write;
    alignas(CL) AlignedAtomic _read;
    alignas(CL) Slot _slots[Capacity];
    static constexpr size_t _mask = Capacity - 1;
};
