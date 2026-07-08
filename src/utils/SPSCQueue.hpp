#pragma once
#include <atomic>
#include <type_traits>

// ─── SPSCQueue ───
// Single-Producer, Single-Consumer lock-free bounded queue.
// Fixed capacity, drops oldest on overflow (overwrite semantics).
//
// Thread safety:
//   - One writer: push()
//   - One reader: pop()
//   - No mutexes, no CAS loops on common path
//   - Producer and consumer atomics on separate cache lines

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");

    static constexpr size_t CACHE_LINE = 64;

    struct alignas(CACHE_LINE) AlignedAtomic {
        std::atomic<size_t> value{0};
    };

public:
    SPSCQueue() = default;

    ~SPSCQueue() noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        while (r != w) {
            ptr(r)->~T();
            ++r;
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ─── Producer ───
    // Always succeeds; overwrites oldest on overflow.
    void push(const T& value) {
        size_t w = _write.value.load(std::memory_order_relaxed);
        size_t r = _read.value.load(std::memory_order_acquire);

        // Full? Advance read_pos to drop oldest
        if (w - r >= Capacity) {
            T* old = ptr(r);
            old->~T();
            _read.value.store(r + 1, std::memory_order_release);
        }

        // Construct in slot, then publish
        T* slot = ptr(w);
        ::new (slot) T(value);
        _write.value.store(w + 1, std::memory_order_release);
    }

    // ─── Consumer ───
    // Read one item into `out`. Returns false if empty.
    bool pop(T& out) noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);

        if (r == w)
            return false;

        T* item = ptr(r);
        out = std::move(*item);
        item->~T();
        _read.value.store(r + 1, std::memory_order_release);
        return true;
    }

    // Peek without consuming (consumer only)
    bool peek(T& out) const noexcept {
        size_t r = _read.value.load(std::memory_order_relaxed);
        size_t w = _write.value.load(std::memory_order_acquire);
        if (r == w) return false;
        out = *ptr(r);
        return true;
    }

    // Approximate number of items available (consumer side)
    size_t size() const noexcept {
        size_t w = _write.value.load(std::memory_order_acquire);
        size_t r = _read.value.load(std::memory_order_relaxed);
        return w - r;
    }

    bool empty() const noexcept { return size() == 0; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    T* ptr(size_t idx) noexcept {
        return reinterpret_cast<T*>(&_buffer[(idx % Capacity) * sizeof(T)]);
    }
    const T* ptr(size_t idx) const noexcept {
        return reinterpret_cast<const T*>(&_buffer[(idx % Capacity) * sizeof(T)]);
    }

    // Producer cache line
    alignas(CACHE_LINE) AlignedAtomic _write;
    char _pad1[CACHE_LINE - sizeof(AlignedAtomic)];

    // Consumer cache line
    alignas(CACHE_LINE) AlignedAtomic _read;
    char _pad2[CACHE_LINE - sizeof(AlignedAtomic)];

    // Storage
    alignas(T) std::byte _buffer[Capacity * sizeof(T)];
};
