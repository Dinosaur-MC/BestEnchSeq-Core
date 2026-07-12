#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

// ─── BoundedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free bounded queue.
//
// Design doc: docs/MPMCQueue.md
// Algorithm:  Dmitry Vyukov's bounded MPMC (sequence-number + ring buffer)
//
// Lock-free guarantee:  single CAS per push/pop, zero mutex throughout.
//
// ┌─ Characteristics ─────────────────────────────────────────────────┐
// │ Fixed capacity (power-of-2, min 2).                               │
// │ Zero blocking: full → push() returns false, never spins.          │
// │ Single CAS per push/pop.                                          │
// │ uint64_t sequence per slot encodes free/ready/consumed tri-state. │
// └───────────────────────────────────────────────────────────────────┘
//
// ┌─ Memory ordering ─────────────────────────────────────────────────┐
// │ slot.sequence: acquire/release create happens-before for data.    │
// │ head_ / tail_:  relaxed (synch flows through sequence, not head). │
// └───────────────────────────────────────────────────────────────────┘
//
// ┌─ Thread safety ───────────────────────────────────────────────────┐
// │ Any number of concurrent producers  — push() / try_push()         │
// │ Any number of concurrent consumers — pop() / try_pop()            │
// │ Not safe: concurrent push/pop on the *same* logical slot          │
// │          (sequence protocol guarantees this never happens)        │
// └───────────────────────────────────────────────────────────────────┘
//
// Requirements:
//   T: nothrow destructible, nothrow move-assignable.
//   Capacity: power of two ≥ 2.
//
// Reference:
//   Dmitry Vyukov, "Bounded MPMC queue"
//   https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

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

        // Access the live T object stored in this slot.
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
    using value_type = T;

    BoundedMPMCQueue() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            _slots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~BoundedMPMCQueue() noexcept {
        // Drain any remaining items (exclusive access during destruction).
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_relaxed);
        while (r != w) {
            _slots[r & _mask].ptr()->~T();
            ++r;
        }
    }

    BoundedMPMCQueue(const BoundedMPMCQueue&) = delete;
    BoundedMPMCQueue& operator=(const BoundedMPMCQueue&) = delete;

    // ─── Producer ─────────────────────────────────────────────────────
    // Push a value. Returns false if the queue is full (no blocking).
    template <typename U>
    bool push(U&& value) noexcept {
        size_t pos = _write.value.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);

            if (diff == 0) {
                // Slot is free for this position. Try to claim it.
                if (_write.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {

                    // ── Construct data in-place ──
                    ::new (slot.ptr()) T(std::forward<U>(value));

                    // Release: make the data write visible to consumers.
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed: pos was updated to current write head by CAS.
                // Loop back — don't reload pos, CAS already did.
            } else if (diff < 0) {
                // sequence < pos: consumer hasn't read previous cycle → full
                return false;
            } else {
                // diff > 0: another producer claimed this slot.
                // Reload the current write head.
                pos = _write.value.load(std::memory_order_relaxed);
            }
        }
    }

    // ─── Consumer ─────────────────────────────────────────────────────
    // Pop a value into `out`. Returns false if the queue is empty.
    bool pop(T& out) noexcept {
        size_t pos = _read.value.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = _slots[pos & _mask];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(seq) -
                           static_cast<int64_t>(pos + 1);

            if (diff == 0) {
                // Data is ready. Try to claim it.
                if (_read.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {

                    // Move data out and destroy the in-place object.
                    out = std::move(*slot.ptr());
                    slot.ptr()->~T();

                    // Release: mark slot as "consumed and avail for next cycle"
                    slot.sequence.store(pos + Capacity,
                                        std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // sequence <= pos: no data yet → empty
                return false;
            } else {
                // diff > 0: another consumer claimed this slot.
                pos = _read.value.load(std::memory_order_relaxed);
            }
        }
    }

    // ─── Observers ────────────────────────────────────────────────────

    /// Approximate number of elements in the queue.
    /// Under concurrent push/pop this is a point-in-time estimate.
    size_t size() const noexcept {
        size_t w = _write.value.load(std::memory_order_acquire);
        size_t r = _read.value.load(std::memory_order_relaxed);
        return w > r ? w - r : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    static constexpr size_t capacity() noexcept { return Capacity; }

    // Backward-compatible aliases matching SPSCQueue naming.
    template <typename U>
    bool try_push(U&& value) noexcept { return push(std::forward<U>(value)); }
    bool try_pop(T& out) noexcept { return pop(out); }

private:
    // ── Cache-line padded atomics for head (write) and tail (read) ──

    // Write index (producer side): exclusively updated by producers
    alignas(CL) AlignedAtomic _write;

    // Read index (consumer side): exclusively updated by consumers
    alignas(CL) AlignedAtomic _read;

    // Slot storage
    alignas(CL) Slot _slots[Capacity];

    static constexpr size_t _mask = Capacity - 1;
};
