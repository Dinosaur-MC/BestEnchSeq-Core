#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>
#include <type_traits>

// ─── SegmentedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free UNBOUNDED queue.
// Segmented singly-linked list of fixed-size blocks.
// try_push() always returns true (unbounded).
// Inherits from IQueue<T>.
//
// Requirements: T nothrow-destructible and nothrow-move-assignable,
//               BlockSize power of two ≥ 64.

template <typename T, size_t BlockSize = 1024>
class SegmentedMPMCQueue final : public IQueue<T> {
    static_assert(BlockSize >= 64, "BlockSize must be at least 64");
    static_assert((BlockSize & (BlockSize - 1)) == 0,
                  "BlockSize must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

    static constexpr size_t CL = 64;

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
                if (seq < base_ticket + i + BlockSize) continue;
                if (seq < base_ticket + i + 2 * BlockSize)
                    std::launder(reinterpret_cast<T*>(data + i * sizeof(T)))->~T();
            }
        }

        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;

        T* slot_at(size_t idx) noexcept {
            return std::launder(reinterpret_cast<T*>(data + idx * sizeof(T)));
        }
    };

    // Claim a global enqueue ticket and locate the slot.
    // Returns (block, idx) via out params.  Does not touch T.
    void claim_enqueue_slot(Block*& out_block, size_t& out_idx) noexcept {
        uint64_t pos = enqueue_pos_.fetch_add(1, std::memory_order_release);
        Block* block = tail_block_.load(std::memory_order_acquire);

        while (pos >= block->base_ticket + BlockSize) [[unlikely]] {
            Block* next = block->next.load(std::memory_order_acquire);
            if (next) [[likely]] {
                tail_block_.compare_exchange_weak(block, next,
                    std::memory_order_relaxed, std::memory_order_relaxed);
                block = next; continue;
            }
            auto* nb = new Block(block->base_ticket + BlockSize);
            Block* exp = nullptr;
            if (block->next.compare_exchange_strong(exp, nb,
                    std::memory_order_release, std::memory_order_relaxed)) [[likely]]
                next = nb;
            else { delete nb; next = exp; }
            tail_block_.compare_exchange_weak(block, next,
                std::memory_order_release, std::memory_order_relaxed);
            block = next;
        }

        if (pos < block->base_ticket) [[unlikely]] {
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize)
                block = block->next.load(std::memory_order_acquire);
        }

        size_t idx = static_cast<size_t>(pos - block->base_ticket);
        while (block->sequences[idx].load(std::memory_order_acquire) != pos)
            std::this_thread::yield();

        out_block = block;
        out_idx = idx;
    }

public:
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    SegmentedMPMCQueue()
        : root_block_(new Block(0))
    {
        head_block_.store(root_block_, std::memory_order_relaxed);
        tail_block_.store(root_block_, std::memory_order_relaxed);
    }

    ~SegmentedMPMCQueue() noexcept final {
        Block* b = root_block_;
        while (b) { Block* n = b->next.load(std::memory_order_relaxed); delete b; b = n; }
    }

    SegmentedMPMCQueue(const SegmentedMPMCQueue&) = delete;
    SegmentedMPMCQueue& operator=(const SegmentedMPMCQueue&) = delete;

    // ─── Producer ─────────────────────────────────────────────────────

    bool try_push(const T& value) noexcept override final {
        if constexpr (std::is_copy_constructible_v<T>) {
            Block* block; size_t idx;
            claim_enqueue_slot(block, idx);
            ::new (block->slot_at(idx)) T(value);
            block->sequences[idx].store(idx + block->base_ticket + BlockSize,
                                        std::memory_order_release);
            return true;
        }
        return false;
    }

    bool try_push(T&& value) noexcept override final {
        Block* block; size_t idx;
        claim_enqueue_slot(block, idx);
        ::new (block->slot_at(idx)) T(std::move(value));
        block->sequences[idx].store(idx + block->base_ticket + BlockSize,
                                    std::memory_order_release);
        return true;
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_emplace(Args&&... args) noexcept {
        Block* block; size_t idx;
        claim_enqueue_slot(block, idx);
        ::new (block->slot_at(idx)) T(std::forward<Args>(args)...);
        block->sequences[idx].store(idx + block->base_ticket + BlockSize,
                                    std::memory_order_release);
        return true;
    }

    // ─── Consumer ─────────────────────────────────────────────────────

    bool try_pop(T& out) noexcept override final {
        uint64_t pos;
        for (;;) {
            pos = dequeue_pos_.load(std::memory_order_relaxed);
            uint64_t enq = enqueue_pos_.load(std::memory_order_acquire);
            if (pos >= enq) return false;
            if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
                break;
        }

        Block* block = head_block_.load(std::memory_order_acquire);
        while (pos >= block->base_ticket + BlockSize) {
            Block* n = block->next.load(std::memory_order_acquire);
            if (!n) { std::this_thread::yield(); continue; }
            head_block_.compare_exchange_weak(block, n,
                std::memory_order_release, std::memory_order_relaxed);
            block = n;
        }

        if (pos < block->base_ticket) [[unlikely]] {
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize)
                block = block->next.load(std::memory_order_acquire);
        }

        size_t idx = static_cast<size_t>(pos - block->base_ticket);
        while (block->sequences[idx].load(std::memory_order_acquire) != pos + BlockSize)
            std::this_thread::yield();

        T* slot = block->slot_at(idx);
        out = std::move(*slot);
        slot->~T();
        block->sequences[idx].store(pos + 2 * BlockSize, std::memory_order_relaxed);

        if (idx == BlockSize - 1) {
            Block* n = block->next.load(std::memory_order_acquire);
            if (n) {
                Block* e = block;
                head_block_.compare_exchange_strong(e, n, std::memory_order_release);
            }
        }
        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    size_t size() const noexcept override final {
        uint64_t w = enqueue_pos_.load(std::memory_order_acquire);
        uint64_t r = dequeue_pos_.load(std::memory_order_relaxed);
        return w > r ? static_cast<size_t>(w - r) : 0;
    }

    bool empty() const noexcept override final { return size() == 0; }
    size_t capacity() const noexcept override final { return 0; }

    void clear() noexcept override final {
        if constexpr (std::is_default_constructible_v<T>) {
            T item;
            while (try_pop(item)) {}
        }
    }

private:
    alignas(CL) std::atomic<uint64_t> enqueue_pos_{0};
    alignas(CL) std::atomic<uint64_t> dequeue_pos_{0};
    alignas(CL) std::atomic<Block*> head_block_{nullptr};
    alignas(CL) std::atomic<Block*> tail_block_{nullptr};
    Block* const root_block_;
};
