#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ─── SPMCQueue ───
// Single-Producer, Multi-Consumer lock-free bounded queue.
// Fixed capacity (power of 2), drops oldest value on overflow.
//
// Thread safety:
//   - One writer thread — push() uses relaxed atomic + release store
//   - Multiple reader threads — read()/read_cursor() use acquire loads
//   - No mutexes, no spin-wait, no CAS loops on common path
//   - Producer never blocks for consumers; overwrites stale data
//
// Generation protocol:
//   Each slot has a generation counter. Let n = seq / Capacity.
//   Slot is writable when generation == n * 2   (even = empty).
//   Slot is readable when generation == n * 2 + 1 (odd = filled).
//   Generation increment by 2 each cycle, wrapping at ~4B.

template <typename T, size_t Capacity>
class SPMCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    struct Slot {
        T value;
        std::atomic<uint32_t> generation;
    };

public:
    SPMCQueue() : _write_idx(0) {
        // Construct slots in-place (Slot has atomic member, not movable)
        for (size_t i = 0; i < Capacity; ++i)
            new (&_slots[i]) Slot{{}, 0};
    }

    ~SPMCQueue() {
        // Destroy slots — only if T has a non-trivial destructor
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
        }
    }

    // Non-copyable, non-movable
    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;

    // ─── Producer ───
    // Push a value. O(1), non-blocking. Overwrites oldest on overflow.
    uint64_t push(const T& value) {
        uint64_t write_seq = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t slot = write_seq & (Capacity - 1);
        _slots[slot].value = value;
        _slots[slot].generation.store(
            static_cast<uint32_t>((write_seq / Capacity) * 2 + 1),
            std::memory_order_release);
        return write_seq;
    }

    // ─── Consumer ───
    struct Cursor {
        uint64_t next_seq;
    };

    // Create cursor starting at the oldest un-overwritten position.
    Cursor read_cursor() const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        uint64_t start = write >= Capacity ? write - Capacity + 1 : 0;
        return {start};
    }

    // Read next value for cursor. Returns nullptr if no data (caller retries).
    // Advances cursor. On data loss (overwritten), jumps forward.
    const T* read(Cursor& cursor) const noexcept {
        uint64_t seq = cursor.next_seq;
        size_t slot = seq & (Capacity - 1);
        uint32_t expected = static_cast<uint32_t>((seq / Capacity) * 2 + 1);

        if (_slots[slot].generation.load(std::memory_order_acquire) == expected) {
            cursor.next_seq = seq + 1;
            return &_slots[slot].value;
        }

        // Check if our position was overwritten
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        if (write > seq + Capacity) {
            cursor.next_seq = write >= Capacity ? write - Capacity + 1 : 0;
        }
        return nullptr;
    }

    // Peek without advancing cursor.
    const T* peek(const Cursor& cursor) const noexcept {
        uint64_t seq = cursor.next_seq;
        size_t slot = seq & (Capacity - 1);
        uint32_t expected = static_cast<uint32_t>((seq / Capacity) * 2 + 1);
        return (_slots[slot].generation.load(std::memory_order_acquire) == expected)
               ? &_slots[slot].value : nullptr;
    }

    // Number of items available (estimated, may change).
    size_t available(const Cursor& cursor) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return (write > cursor.next_seq) ? static_cast<size_t>(write - cursor.next_seq) : 0;
    }

    // Total items pushed (monotonic).
    uint64_t count() const noexcept {
        return _write_idx.load(std::memory_order_acquire);
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    // Write index (producer only)
    alignas(64) std::atomic<uint64_t> _write_idx;

    // Ring buffer slots — raw memory, manually constructed
    alignas(64) Slot _slots[Capacity];
};
