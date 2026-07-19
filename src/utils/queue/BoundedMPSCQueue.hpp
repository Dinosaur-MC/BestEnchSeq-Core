#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>
#include <type_traits>

// Portably expose _mm_pause() on x86.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_AMD64) || defined(_M_IX86)
#include <immintrin.h>
#define BESQ_PAUSE() _mm_pause()
#else
#define BESQ_PAUSE() ((void)0)
#endif

// ─── BoundedMPSCQueue ───
// Multi-Producer, Single-Consumer lock-free BOUNDED queue.
// Ring-buffer (Dmitry Vyukov sequence-number algorithm) optimized for a
// single consumer and single-consumer-friendly producers.
//
// Producers use fetch_add (not CAS) to claim slots — since there is
// exactly one consumer the slot sequence advances monotonically per
// cycle, making CAS unnecessary.  This eliminates CAS retry storms
// under high producer contention.  A capacity pre-check via the
// consumer's _read cursor keeps try_push accurate.
//
// The consumer reads _read.value with a relaxed load and checks the
// slot's sequence — no CAS, no retry loop.
//
// Inherits from IQueue<T> for runtime-polymorphic access.
//
// Thread safety:
//   - Multiple writers:  try_push() / try_emplace()
//   - One reader:        try_pop() / clear()
//
// clear() and the destructor must be called only when no producers are
// concurrently enqueuing.  In practice this means the consumer thread
// calls clear() during a quiet period, or the owning thread drains the
// queue after joining all producers.
//
// Requirements:
//   T nothrow-destructible, nothrow-move-assignable and
//   nothrow-move-constructible.
//   Capacity power of two ≥ 2.

template <typename T, size_t Capacity>
class BoundedMPSCQueue final : public IQueue<T> {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "T must be nothrow move constructible");

    static constexpr size_t CL = 64;

    struct alignas(CL) AlignedAtomic {
        std::atomic<size_t> value{0};
    };

    struct alignas(CL) Slot {
        std::atomic<uint64_t> sequence;
        alignas(T) unsigned char _storage[sizeof(T)];
        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(_storage)); }
        const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(_storage)); }
    };

    // ── Producer slot claim (fetch_add, multi-threaded) ─────────────
    // Unlike MPMC (which requires CAS to arbitrate among producers),
    // MPSC can use a simpler fetch_add: with one consumer the slot
    // sequence advances monotonically per cycle.  fetch_add eliminates
    // the CAS retry storm under high producer contention.
    //
    // A capacity pre-check avoids oversubscription in the common case;
    // if a race still pushes past Capacity the producer spins briefly
    // on the slot sequence waiting for the consumer to free it.
    bool claim_write_slot(size_t& pos) noexcept {
        size_t r = _read.value.load(std::memory_order_acquire);
        size_t w = _write.value.load(std::memory_order_relaxed);
        if (w - r >= Capacity) [[unlikely]]
            return false;

        pos = _write.value.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // ── Consumer slot claim (single-threaded, no CAS) ───────────────
    bool claim_read_slot(size_t& pos) noexcept {
        pos = _read.value.load(std::memory_order_relaxed);
        Slot& slot = _slots[pos & _mask];
        uint64_t seq = slot.sequence.load(std::memory_order_acquire);
        if (seq == pos + 1) {           // slot is filled and ready
            _read.value.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;                   // empty or not yet written
    }

public:
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    BoundedMPSCQueue() noexcept {
        for (size_t i = 0; i < Capacity; ++i)
            _slots[i].sequence.store(i, std::memory_order_relaxed);
    }

    ~BoundedMPSCQueue() noexcept final {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_relaxed);
        while (r != w) {
            size_t idx = r & _mask;
            // Only destruct if the slot was fully written (defensive check
            // against a producer that fetch_add'ed _write but hasn't finished
            // placement new yet — extremely rare but possible).
            if (_slots[idx].sequence.load(std::memory_order_relaxed) == r + 1)
                _slots[idx].ptr()->~T();
            ++r;
        }
    }

    BoundedMPSCQueue(const BoundedMPSCQueue&) = delete;
    BoundedMPSCQueue& operator=(const BoundedMPSCQueue&) = delete;

    // ─── Producer API ───────────────────────────────────────────────

    bool try_push(const T& value) noexcept override final {
        if constexpr (std::is_copy_constructible_v<T>) {
            size_t pos;
            if (!claim_write_slot(pos)) return false;
            auto& slot = _slots[pos & _mask];
            while (slot.sequence.load(std::memory_order_acquire) != pos)
                BESQ_PAUSE();
            ::new (slot.ptr()) T(value);
            slot.sequence.store(pos + 1, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool try_push(T&& value) noexcept override final {
        size_t pos;
        if (!claim_write_slot(pos)) return false;
        auto& slot = _slots[pos & _mask];
        while (slot.sequence.load(std::memory_order_acquire) != pos)
            std::this_thread::yield();
        ::new (slot.ptr()) T(std::move(value));
        slot.sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept {
        size_t pos;
        if (!claim_write_slot(pos)) return false;
        auto& slot = _slots[pos & _mask];
        while (slot.sequence.load(std::memory_order_acquire) != pos)
            std::this_thread::yield();
        ::new (slot.ptr()) T(std::forward<Args>(args)...);
        slot.sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ─── Consumer API ───────────────────────────────────────────────

    bool try_pop(T& out) noexcept override final {
        size_t pos;
        if (!claim_read_slot(pos)) return false;
        auto& slot = _slots[pos & _mask];
        out = std::move(*slot.ptr());
        slot.ptr()->~T();
        slot.sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    // ─── Observers ─────────────────────────────────────────────────

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
        while (r != w) {
            size_t idx = r & _mask;
            auto& slot = _slots[idx];
            // Wait for the producer to finish writing if it hasn't yet.
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            if (seq == r + 1) {
                slot.ptr()->~T();
                // Advance sequence to the reclaim phase so producers can
                // reuse this slot.  Without this the sequence stays at
                // r+1 and subsequent producers compute diff<0 and
                // permanently think the queue is full.
                slot.sequence.store(r + Capacity, std::memory_order_release);
            }
            ++r;
        }
        _read.value.store(w, std::memory_order_release);
    }

private:
    alignas(CL) AlignedAtomic _write;
    alignas(CL) AlignedAtomic _read;
    alignas(CL) Slot _slots[Capacity];
    static constexpr size_t _mask = Capacity - 1;
};
