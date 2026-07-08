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
//   Each slot has a generation counter. Let n = write_seq / Capacity.
//   Slot is writable when generation == n * 2   (even = empty).
//   Slot is readable when generation == n * 2 + 1 (odd = filled).
//   Generation uses uint64_t to avoid ABA wrapping.

template <typename T, size_t Capacity>
class SPMCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    struct Slot {
        T value;
        std::atomic<uint64_t> generation{0};
    };

public:
    SPMCQueue() : _write_idx(0) {
        // Slots are default-constructed (generation initialized to 0).
        // For non-trivially-constructible T, value is value-initialized.
        for (size_t i = 0; i < Capacity; ++i)
            new (&_slots[i]) Slot();
    }

    ~SPMCQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
        }
    }

    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;

    // ─── Producer ───
    // Push a value. O(1), non-blocking. Overwrites oldest on overflow.
    // If T has a non-trivial destructor, destroys the overwritten value.
    void push(const T& value) {
        uint64_t write_seq = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t slot_idx = write_seq & (Capacity - 1);
        Slot& s = _slots[slot_idx];

        // Destroy the old value before overwriting (prevents resource leak)
        if constexpr (!std::is_trivially_destructible_v<T>)
            s.value.~T();

        // Construct new value in-place
        new (&s.value) T(value);

        // Make the slot readable: generation = (epoch * 2) + 1
        s.generation.store((write_seq / Capacity) * 2 + 1,
                           std::memory_order_release);
    }

    // ─── Consumer ───
    struct Cursor {
        uint64_t next_seq;
    };

    // Create cursor starting at the oldest un-overwritten position.
    Cursor read_cursor() const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        uint64_t start = write >= Capacity ? write - Capacity : 0;
        return {start};
    }

    // Copy the next value into `out`. Returns false if no data yet.
    // On data loss (overwritten), cursor jumps forward; returns false.
    bool read(Cursor& cursor, T& out) const noexcept {
        uint64_t seq = cursor.next_seq;
        size_t slot_idx = seq & (Capacity - 1);
        uint64_t expected = (seq / Capacity) * 2 + 1;

        if (_slots[slot_idx].generation.load(std::memory_order_acquire) == expected) {
            out = _slots[slot_idx].value;
            cursor.next_seq = seq + 1;
            return true;
        }

        // Check if our position was overwritten
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        if (write > seq + Capacity) {
            cursor.next_seq = write >= Capacity ? write - Capacity : 0;
        }
        return false;
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
    alignas(64) std::atomic<uint64_t> _write_idx;
    alignas(64) Slot _slots[Capacity];
};
