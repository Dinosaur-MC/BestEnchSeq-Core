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
// Consumer API (different from other queue types):
//   auto cursor = q.read_cursor();
//   T val;
//   while (q.read(cursor, val)) { ... }

template <typename T, size_t Capacity>
class SPMCQueue final : public IQueue<T> {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    struct Slot {
        T value;
        std::atomic<uint64_t> generation{0};
    };

    template <typename U>
    void write_slot(U&& value) {
        uint64_t ws = _write_idx.fetch_add(1, std::memory_order_relaxed);
        size_t i = ws & (Capacity - 1);
        if constexpr (!std::is_trivially_destructible_v<T>)
            _slots[i].value.~T();
        new (&_slots[i].value) T(std::forward<U>(value));
        _slots[i].generation.store((ws / Capacity) * 2 + 1, std::memory_order_release);
    }

public:
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    struct Cursor { uint64_t next_seq; };

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

    // ─── Producer ─────────────────────────────────────────────────────

    bool try_push(const T& value) noexcept override final {
        if constexpr (!std::is_copy_constructible_v<T>)
            return false;
        write_slot(value);
        return true;
    }

    bool try_push(T&& value) noexcept override final {
        write_slot(std::move(value));
        return true;
    }

    // ─── Consumer (cursor-based) ─────────────────────────────────────

    Cursor read_cursor() const noexcept {
        uint64_t w = _write_idx.load(std::memory_order_acquire);
        return {w >= Capacity ? w - Capacity : 0};
    }

    bool read(Cursor& c, T& out) noexcept {
        uint64_t seq = c.next_seq;
        size_t i = seq & (Capacity - 1);
        uint64_t expected = (seq / Capacity) * 2 + 1;
        if (_slots[i].generation.load(std::memory_order_acquire) != expected)
            return overwrite_check(c, seq);
        out = _slots[i].value;
        if (_slots[i].generation.load(std::memory_order_acquire) != expected)
            return overwrite_check(c, seq);
        c.next_seq = seq + 1;
        return true;
    }

    size_t available(const Cursor& c) const noexcept {
        uint64_t w = _write_idx.load(std::memory_order_acquire);
        return w > c.next_seq ? static_cast<size_t>(w - c.next_seq) : 0;
    }

    /// Total elements ever pushed (monotonic).
    uint64_t count() const noexcept {
        return _write_idx.load(std::memory_order_acquire);
    }

    // ─── IQueue<T> overrides ─────────────────────────────────────────

    bool try_pop(T& out) noexcept override final {
        Cursor c = read_cursor();
        return read(c, out);
    }

    size_t size() const noexcept override final {
        Cursor c = read_cursor();
        return available(c);
    }

    bool empty() const noexcept override final { return size() == 0; }
    size_t capacity() const noexcept override final { return Capacity; }

    void clear() noexcept override final {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < Capacity; ++i) _slots[i].value.~T();
            for (size_t i = 0; i < Capacity; ++i) {
                new (&_slots[i]) Slot();
                _slots[i].generation.store(0, std::memory_order_relaxed);
            }
        }
        _write_idx.store(0, std::memory_order_release);
    }

private:
    bool overwrite_check(Cursor& c, uint64_t seq) const noexcept {
        uint64_t w = _write_idx.load(std::memory_order_acquire);
        if (w >= seq + Capacity)
            c.next_seq = w >= Capacity ? w - Capacity : 0;
        return false;
    }

    alignas(64) std::atomic<uint64_t> _write_idx;
    alignas(64) Slot _slots[Capacity];
};
