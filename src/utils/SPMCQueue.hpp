#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

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
        for (size_t i = 0; i < Capacity; ++i)
            new (&_slots[i]) Slot();
    }

    ~SPMCQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>)
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
    }

    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;

    // ─── Producer ───
    void push(const T& value) {
        uint64_t write_seq = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t slot_idx = write_seq & (Capacity - 1);
        Slot& s = _slots[slot_idx];

        if constexpr (!std::is_trivially_destructible_v<T>)
            s.value.~T();
        new (&s.value) T(value);

        s.generation.store((write_seq / Capacity) * 2 + 1,
                           std::memory_order_release);
    }

    // ─── Consumer ───
    struct Cursor { uint64_t next_seq; };

    // Create cursor at the oldest sequence still likely readable.
    // read() will detect overwrites and advance past them.
    Cursor read_cursor() const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return {write >= Capacity ? write - Capacity : 0};
    }

    // Copy next value into `out`. Double-check generation after copy
    // to prevent TOCTOU race (producer overwrites slot during read).
    bool read(Cursor& cursor, T& out) noexcept {
        uint64_t seq = cursor.next_seq;
        size_t slot_idx = seq & (Capacity - 1);
        uint64_t expected = (seq / Capacity) * 2 + 1;
        Slot& s = _slots[slot_idx];

        if (s.generation.load(std::memory_order_acquire) != expected)
            return _overwrite_check(cursor, seq);

        out = s.value;

        if (s.generation.load(std::memory_order_acquire) != expected)
            return _overwrite_check(cursor, seq);

        cursor.next_seq = seq + 1;
        return true;
    }

    size_t available(const Cursor& cursor) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return (write > cursor.next_seq)
               ? static_cast<size_t>(write - cursor.next_seq) : 0;
    }

    uint64_t count() const noexcept {
        return _write_idx.load(std::memory_order_acquire);
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    bool _overwrite_check(Cursor& cursor, uint64_t seq) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        // Slot at seq was reused when write >= seq + Capacity.
        // Jump to the oldest sequence still in the buffer.
        if (write >= seq + Capacity) {
            cursor.next_seq = write >= Capacity ? write - Capacity : 0;
        }
        return false;
    }

    alignas(64) std::atomic<uint64_t> _write_idx;
    alignas(64) Slot _slots[Capacity];
};
