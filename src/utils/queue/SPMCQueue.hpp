#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ─── SPMCQueue ───
// Single-Producer, Multi-Consumer lock-free bounded queue.
//
// Each consumer creates an independent Cursor via read_cursor(); the
// producer overwrites the oldest unread elements when capacity is
// exceeded.  A slow consumer may skip elements that were overwritten
// before it read them.
//
// Inherits from IQueue<T> for runtime-polymorphic access.
//
// Thread safety:
//   - One writer: try_push()
//   - Many readers: each reader owns a Cursor and calls read()
//
// Consumer API (different from other queue types):
//   auto cursor = q.read_cursor();
//   T val;
//   while (q.read(cursor, val)) { ... }
//   size_t avail = q.available(cursor);

template <typename T, size_t Capacity>
class SPMCQueue final : public IQueue<T> {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    using Base = IQueue<T>;

    struct Slot {
        T value;
        std::atomic<uint64_t> generation{0};
    };

public:
    // ─── STL style type aliases ───────────────────────────────────────
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Cursor (consumer position) ───────────────────────────────────
    struct Cursor { uint64_t next_seq; };

    // ─── Construction / destruction ───────────────────────────────────

    SPMCQueue() : _write_idx(0) {
        for (size_t i = 0; i < Capacity; ++i)
            new (&_slots[i]) Slot();
    }

    ~SPMCQueue() final {
        if constexpr (!std::is_trivially_destructible_v<T>)
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
    }

    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a copy.  Overwrites oldest element if full.  Never blocks.
    bool try_push(const T& value) noexcept override {
        if constexpr (std::is_copy_constructible_v<T>) {
            write_slot(value);
            return true;
        } else {
            return false;
        }
    }

    /// Push by move.  Overwrites oldest element if full.
    bool try_push(T&& value) noexcept override {
        write_slot(std::move(value));
        return true;
    }

    // ─── Consumer API (cursor-based) ─────────────────────────────────

    /// Cursor positioned at the oldest readable sequence.
    Cursor read_cursor() const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return {write >= Capacity ? write - Capacity : 0};
    }

    /// Non-blocking read.  Returns false if slot was overwritten or
    /// not yet written.  Advances cursor on success.
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

    /// Number of elements available for `cursor`.
    size_t available(const Cursor& cursor) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return (write > cursor.next_seq)
               ? static_cast<size_t>(write - cursor.next_seq) : 0;
    }

    /// Total number of elements ever pushed (monotonic).
    uint64_t count() const noexcept {
        return _write_idx.load(std::memory_order_acquire);
    }

    // ─── IQueue<T> overrides ─────────────────────────────────────────
    //
    //  try_pop is provided for IQueue conformance but is inefficient
    //  for SPMCQueue (creates a fresh cursor each call).
    //  Prefer the cursor-based read() API for bulk consumption.

    bool try_pop(T& out) noexcept override {
        Cursor c = read_cursor();
        return read(c, out);
    }

    size_t size() const noexcept override {
        Cursor c = read_cursor();
        return available(c);
    }

    bool empty() const noexcept override { return size() == 0; }

    size_t capacity() const noexcept override { return Capacity; }

    void clear() noexcept override {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
            for (size_t i = 0; i < Capacity; ++i) {
                new (&_slots[i]) Slot();
                _slots[i].generation.store(0, std::memory_order_relaxed);
            }
        }
        _write_idx.store(0, std::memory_order_release);
    }

private:
    template <typename U>
    void write_slot(U&& value) {
        uint64_t write_seq = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t slot_idx = write_seq & (Capacity - 1);
        Slot& s = _slots[slot_idx];

        if constexpr (!std::is_trivially_destructible_v<T>)
            s.value.~T();
        new (&s.value) T(std::forward<U>(value));

        s.generation.store((write_seq / Capacity) * 2 + 1,
                           std::memory_order_release);
    }

    bool _overwrite_check(Cursor& cursor, uint64_t seq) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        if (write >= seq + Capacity)
            cursor.next_seq = write >= Capacity ? write - Capacity : 0;
        return false;
    }

    alignas(64) std::atomic<uint64_t> _write_idx;
    alignas(64) Slot _slots[Capacity];
};
