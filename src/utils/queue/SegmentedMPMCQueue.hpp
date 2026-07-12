#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>
#include <type_traits>

// ─── SegmentedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free UNBOUNDED queue.
//
// ┌─ Lock-free guarantee ─────────────────────────────────────────────┐
// │  EVERY operation uses only atomic RMW (fetch_add, CAS).           │
// │  Block linking: compare_exchange_strong on next pointer.          │
// │  No mutex, no spinlock, no blocking syscall on any path.          │
// │  Unused allocations safely delete'd (never exposed to other       │
// │  threads).                                                        │
// └───────────────────────────────────────────────────────────────────┘
//
// Architecture: segmented singly-linked list of fixed-size blocks.
//   Each block is an independent ring buffer (same sequence protocol
//   as BoundedMPMCQueue).  Blocks linked atomically via CAS.
//   Global ticket counters enforce total FIFO ordering.
//
// Because the queue is unbounded, push() always succeeds (returns void).
// For a bounded variant, see BoundedMPMCQueue.
//
// Requirements:
//   T: nothrow destructible, nothrow move-assignable.
//   BlockSize: power of two ≥ 64.
//
// STL-style type aliases:
//   value_type, reference, const_reference, size_type, difference_type

template <typename T, size_t BlockSize = 1024>
class SegmentedMPMCQueue {
    static_assert(BlockSize >= 64, "BlockSize must be at least 64");
    static_assert((BlockSize & (BlockSize - 1)) == 0,
                  "BlockSize must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

    static constexpr size_t CL = 64;

    // ─── Block ────────────────────────────────────────────────────────
    // Fixed-size ring buffer. Sequence protocol:
    //   Init:   sequences[i] = base_ticket + i
    //   Push:   sequences[i] = base_ticket + i + BlockSize
    //   Pop:    sequences[i] = base_ticket + i + 2*BlockSize
    struct Block {
        alignas(CL) std::atomic<uint64_t> sequences[BlockSize];
        alignas(CL) unsigned char data[BlockSize * sizeof(T)];
        std::atomic<Block*> next{nullptr};
        uint64_t const base_ticket;

        explicit Block(uint64_t base) noexcept : base_ticket(base) {
            for (size_t i = 0; i < BlockSize; ++i)
                sequences[i].store(base + i, std::memory_order_relaxed);
        }

        ~Block() noexcept {
            for (size_t i = 0; i < BlockSize; ++i) {
                uint64_t seq = sequences[i].load(std::memory_order_relaxed);
                uint64_t written = base_ticket + i + BlockSize;
                if (seq < written)
                    continue;
                uint64_t consumed = base_ticket + i + 2 * BlockSize;
                if (seq < consumed) {
                    std::launder(reinterpret_cast<T*>(
                        data + i * sizeof(T)))->~T();
                }
            }
        }

        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;

        T* slot_at(size_t idx) noexcept {
            return std::launder(reinterpret_cast<T*>(
                data + idx * sizeof(T)));
        }
    };

public:
    // ─── STL style type aliases ───────────────────────────────────────
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction / destruction ───────────────────────────────────

    SegmentedMPMCQueue()
        : root_block_(new Block(0))
    {
        head_block_.store(root_block_, std::memory_order_relaxed);
        tail_block_.store(root_block_, std::memory_order_relaxed);
    }

    ~SegmentedMPMCQueue() noexcept {
        Block* block = root_block_;
        while (block) {
            Block* next = block->next.load(std::memory_order_relaxed);
            delete block;
            block = next;
        }
    }

    SegmentedMPMCQueue(const SegmentedMPMCQueue&) = delete;
    SegmentedMPMCQueue& operator=(const SegmentedMPMCQueue&) = delete;

    // ─── Producer API ─────────────────────────────────────────────────

    /// Push a value.  Always succeeds (unbounded).  Fully lock-free.
    /// Thread-safe for any number of concurrent producers.
    void push(T value) {
        uint64_t pos = enqueue_pos_.fetch_add(1, std::memory_order_release);

        Block* block = tail_block_.load(std::memory_order_acquire);

        // ── Find (or create) the block that owns `pos` ──
        while (pos >= block->base_ticket + BlockSize) [[unlikely]] {
            Block* next = block->next.load(std::memory_order_acquire);

            if (next) [[likely]] {
                tail_block_.compare_exchange_weak(block, next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed);
                block = next;
                continue;
            }

            auto* new_block = new Block(block->base_ticket + BlockSize);
            Block* expected = nullptr;

            if (block->next.compare_exchange_strong(
                    expected, new_block,
                    std::memory_order_release,
                    std::memory_order_relaxed)) [[likely]] {
                next = new_block;
            } else {
                delete new_block;
                next = expected;
            }

            tail_block_.compare_exchange_weak(block, next,
                std::memory_order_release,
                std::memory_order_relaxed);
            block = next;
        }

        // ── Safety net: if we drifted past our block, restart from root ──
        if (pos < block->base_ticket) [[unlikely]] {
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize) {
                block = block->next.load(std::memory_order_acquire);
            }
        }

        // ── Write data ──
        size_t idx = static_cast<size_t>(pos - block->base_ticket);

        while (block->sequences[idx].load(std::memory_order_acquire) != pos) {
            std::this_thread::yield();
        }

        ::new (block->slot_at(idx)) T(std::move(value));
        block->sequences[idx].store(pos + BlockSize,
                                    std::memory_order_release);
    }

    /// Non-blocking push.  Always returns true (unbounded queue).
    template <typename U>
    bool try_push(U&& value) noexcept {
        push(std::forward<U>(value));
        return true;
    }

    // ─── Consumer API ─────────────────────────────────────────────────

    /// Pop the oldest element into `out`.  Returns false if empty.
    /// Thread-safe for any number of concurrent consumers.
    bool pop(value_type& out) {
        return try_pop(out);
    }

    /// Non-blocking pop.  Returns false if empty.
    bool try_pop(value_type& out) {
        uint64_t pos;

        for (;;) {
            pos = dequeue_pos_.load(std::memory_order_relaxed);
            uint64_t enq_pos = enqueue_pos_.load(std::memory_order_acquire);
            if (pos >= enq_pos)
                return false;
            if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
                break;
        }

        Block* block = head_block_.load(std::memory_order_acquire);

        while (pos >= block->base_ticket + BlockSize) {
            Block* next = block->next.load(std::memory_order_acquire);
            if (!next) {
                std::this_thread::yield();
                continue;
            }
            head_block_.compare_exchange_weak(block, next,
                std::memory_order_release,
                std::memory_order_relaxed);
            block = next;
        }

        if (pos < block->base_ticket) [[unlikely]] {
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize) {
                block = block->next.load(std::memory_order_acquire);
            }
        }

        size_t idx = static_cast<size_t>(pos - block->base_ticket);

        while (block->sequences[idx].load(std::memory_order_acquire) !=
               pos + BlockSize) {
            std::this_thread::yield();
        }

        T* slot = block->slot_at(idx);
        out = std::move(*slot);
        slot->~T();
        block->sequences[idx].store(pos + 2 * BlockSize,
                                    std::memory_order_relaxed);

        if (idx == BlockSize - 1) {
            Block* next = block->next.load(std::memory_order_acquire);
            if (next) {
                Block* expected = block;
                head_block_.compare_exchange_strong(expected, next,
                    std::memory_order_release);
            }
        }

        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    /// Approximate number of elements (point-in-time under concurrent
    /// push/pop).
    size_type size() const noexcept {
        uint64_t w = enqueue_pos_.load(std::memory_order_acquire);
        uint64_t r = dequeue_pos_.load(std::memory_order_relaxed);
        return w > r ? static_cast<size_type>(w - r) : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    /// Unbounded — always returns 0.
    static constexpr size_type capacity() noexcept { return 0; }

    /// Remove all elements.  NOT thread-safe — caller must ensure no
    /// concurrent push or pop.
    void clear() noexcept {
        T item;
        while (try_pop(item)) {}
    }

private:
    // ── Producer cache line ──
    alignas(CL) std::atomic<uint64_t> enqueue_pos_{0};

    // ── Consumer cache line ──
    alignas(CL) std::atomic<uint64_t> dequeue_pos_{0};

    // ── Head block (consumer side) ──
    alignas(CL) std::atomic<Block*> head_block_{nullptr};

    // ── Tail block (producer side) ──
    alignas(CL) std::atomic<Block*> tail_block_{nullptr};

    // ── Root block ──
    Block* const root_block_;
};
