#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

// ─── BoundedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free bounded queue.
//
// Algorithm: Dmitry Vyukov's bounded MPMC (sequence-number ring buffer).
//   https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
//
// Lock-free guarantee: single CAS per push/pop, zero mutex throughout.
//
// Characteristics:
//   - Fixed capacity (power-of-2, min 2).
//   - Zero blocking: full → push returns false, never spins.
//   - Single CAS per push/pop.
//   - uint64_t sequence per slot encodes free/ready/consumed tri-state.
//
// Requirements:
//   T: nothrow destructible, nothrow move-assignable.
//   Capacity: power of two ≥ 2.
//
// STL-style type aliases:
//   value_type, reference, const_reference, size_type, difference_type

template <typename T, size_t Capacity>
class BoundedMPMCQueue {
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

    // Slot with raw storage for T (avoids requiring default-constructible T).
    struct Slot {
        std::atomic<uint64_t> sequence;

        T* ptr() noexcept {
            return std::launder(reinterpret_cast<T*>(_storage));
        }
        const T* ptr() const noexcept {
            return std::launder(reinterpret_cast<const T*>(_storage));
        }

    private:
        alignas(T) unsigned char _storage[sizeof(T)];
    };

    // Each slot has a sequence number. Initially, slot[i].sequence = i.
    //
    // Invariant during operation:
    //   seq == pos         → slot is free for producer at `pos`
    //   seq == pos + 1     → data written, ready for consumer at `pos`
    //   seq == pos + cap   → consumed, ready for next cycle
    //
    // Producer sees diff = seq - pos:
    //   0  → claim slot with CAS, write data, store seq = pos + 1
    //   <0 → queue is full (consumer hasn't read the previous cycle's data)
    //   >0 → another producer claimed this slot, retry with fresh pos
    //
    // Consumer sees diff = seq - (pos + 1):
    //   0  → data ready, claim with CAS, read data, store seq = pos + cap
    //   <0 → queue is empty
    //   >0 → another consumer claimed this slot, retry with fresh pos

public:
    // ─── STL style type aliases ───────────────────────────────────────
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction / destruction ───────────────────────────────────

    BoundedMPMCQueue() noexcept {
        for (size_type i = 0; i < Capacity; ++i)
            _slots[i].sequence.store(i, std::memory_order_relaxed);
    }

    ~BoundedMPMCQueue() noexcept {
        size_type r = _read.value.load(std::memory_order_relaxed);
        size_type w = _write.value.load(std::memory_order_relaxed);
        while (r != w) {
            _slots[r & _mask].ptr()->~T();
            ++r;
        }
    }

    BoundedMPMCQueue(const BoundedMPMCQueue&) = delete;
    BoundedMPMCQueue& operator=(const BoundedMPMCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a value.  Returns false if the queue is full (no blocking).
    /// Thread-safe for any number of concurrent producers.
    template <typename U>
    bool push(U&& value) noexcept {
        size_type pos = _write.value.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);

            if (diff == 0) {
                if (_write.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    ::new (slot.ptr()) T(std::forward<U>(value));
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;   // full
            } else {
                pos = _write.value.load(std::memory_order_relaxed);
            }
        }
    }

    /// Non-blocking push.  Alias for push().
    template <typename U>
    bool try_push(U&& value) noexcept { return push(std::forward<U>(value)); }

    // ─── Consumer API ─────────────────────────────────────────────────

    /// Pop the oldest element into `out`.  Returns false if empty.
    /// Thread-safe for any number of concurrent consumers.
    bool pop(value_type& out) noexcept {
        size_type pos = _read.value.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) -
                           static_cast<int64_t>(pos + 1);

            if (diff == 0) {
                if (_read.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(*slot.ptr());
                    slot.ptr()->~T();
                    slot.sequence.store(pos + Capacity,
                                        std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;   // empty
            } else {
                pos = _read.value.load(std::memory_order_relaxed);
            }
        }
    }

    /// Non-blocking pop.  Alias for pop().
    bool try_pop(value_type& out) noexcept { return pop(out); }

    // ─── Observers ───────────────────────────────────────────────────

    /// Approximate number of elements (point-in-time under concurrent
    /// push/pop).
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
        size_type w = _write.value.load(std::memory_order_relaxed);
        while (r != w) {
            _slots[r & _mask].ptr()->~T();
            ++r;
        }
        _read.value.store(w, std::memory_order_release);
    }

private:
    // ── Cache-line padded atomics ──
    alignas(CL) AlignedAtomic _write;
    alignas(CL) AlignedAtomic _read;

    alignas(CL) Slot _slots[Capacity];

    static constexpr size_type _mask = Capacity - 1;
};
