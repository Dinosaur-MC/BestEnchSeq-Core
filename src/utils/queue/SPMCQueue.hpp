#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ─── SPMCQueue ───
// Single-Producer, Multi-Consumer lock-free bounded queue.
//
// Each consumer creates an independent Cursor via read_cursor(); the
// producer overwrites the oldest unread elements when capacity is
// exceeded.  This means a slow consumer may skip (lose) elements that
// were overwritten before it read them.
//
// Thread safety:
//   - One writer: push() / try_push()
//   - Many readers: each reader thread owns a Cursor and calls read()
//
// Consumer API (different from other queue types):
//   auto cursor = q.read_cursor();       // start reading from oldest slot
//   T val;
//   while (q.read(cursor, val)) { ... }  // non-blocking read
//   size_t avail = q.available(cursor);  // items this cursor can read
//
// STL-style type aliases:
//   value_type, reference, const_reference, size_type, difference_type

template <typename T, size_t Capacity>
class SPMCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

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

    ~SPMCQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>)
            for (size_t i = 0; i < Capacity; ++i)
                _slots[i].value.~T();
    }

    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a copy of `value`.  Overwrites the oldest unread element if
    /// the queue is full.  Never blocks.
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

    /// Push by move.  Overwrites the oldest unread element if full.
    void push(T&& value) {
        uint64_t write_seq = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t slot_idx = write_seq & (Capacity - 1);
        Slot& s = _slots[slot_idx];

        if constexpr (!std::is_trivially_destructible_v<T>)
            s.value.~T();
        new (&s.value) T(std::move(value));

        s.generation.store((write_seq / Capacity) * 2 + 1,
                           std::memory_order_release);
    }

    /// Non-blocking push.  Always returns true (overwrites when full).
    template <typename U>
    bool try_push(U&& value) noexcept {
        push(std::forward<U>(value));
        return true;
    }

    // ─── Consumer API (cursor-based) ─────────────────────────────────

    /// Create a cursor positioned at the oldest sequence still likely
    /// readable.  read() will detect overwrites and advance past them.
    Cursor read_cursor() const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return {write >= Capacity ? write - Capacity : 0};
    }

    /// Non-blocking read into `out` using `cursor`.  Returns false if
    /// the slot at `cursor` has been overwritten or is not yet written.
    /// Advances `cursor` on success.
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

    /// Number of elements available for `cursor` to read (estimate).
    size_t available(const Cursor& cursor) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        return (write > cursor.next_seq)
               ? static_cast<size_t>(write - cursor.next_seq) : 0;
    }

    // ─── Observers ───────────────────────────────────────────────────

    /// Total number of elements ever pushed (monotonic).
    uint64_t count() const noexcept {
        return _write_idx.load(std::memory_order_acquire);
    }

    /// Capacity of the ring buffer.
    static constexpr size_type capacity() noexcept { return Capacity; }

    /// Approximate number of elements (const-based, imprecise).
    size_type size() const noexcept {
        auto c = read_cursor();
        return available(c);
    }

    bool empty() const noexcept { return size() == 0; }

    /// Remove all elements.  NOT thread-safe — caller must ensure no
    /// concurrent push or read.
    void clear() noexcept {
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
    bool _overwrite_check(Cursor& cursor, uint64_t seq) const noexcept {
        uint64_t write = _write_idx.load(std::memory_order_acquire);
        if (write >= seq + Capacity) {
            cursor.next_seq = write >= Capacity ? write - Capacity : 0;
        }
        return false;
    }

    alignas(64) std::atomic<uint64_t> _write_idx;
    alignas(64) Slot _slots[Capacity];
};
