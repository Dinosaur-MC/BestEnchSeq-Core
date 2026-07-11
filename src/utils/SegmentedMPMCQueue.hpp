#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <vector>

// ─── SegmentedMPMCQueue ───
// Multi-Producer, Multi-Consumer lock-free unbounded queue.
//
// Architecture:
//   Segmented linked list of fixed-size blocks. Each block is an independent
//   ring buffer using the same sequence-number protocol as BoundedMPMCQueue.
//   Blocks are allocated on demand; retired blocks form a chain that is
//   walked by consumers.
//
// Lock-freedom:
//   - Per-element operations (slot claim, data write/read) are fully
//     lock-free using atomic sequence numbers (same protocol as BoundedMPMCQueue).
//   - Block allocation/retirement is protected by a mutex. This is acceptable
//     because block transitions happen once per BlockSize (default 1024)
//     operations. The per-element hot path never takes a lock.
//   - Head/tail block pointers are atomic for lock-free reads.
//   - Block memory is stable (never freed until queue destruction), so raw
//     Block* pointers are safe to dereference without ref-counting.
//
// Characteristics:
//   - Unbounded (grows until OOM) — never drops data
//   - Total FIFO ordering across all producers
//   - Cache-line aligned atomics to minimise false sharing
//
// Requirements:
//   T must be nothrow destructible and nothrow move assignable.

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
    // Fixed-size ring buffer. Each slot has a sequence number encoding
    // its lifecycle state (free → written → consumed).
    //
    // Sequence protocol:
    //   Initially:  sequences[i] = base_ticket + i
    //   After push: sequences[i] = base_ticket + i + BlockSize
    //   After pop:  sequences[i] = base_ticket + i + 2*BlockSize
    //
    // Blocks are never reused — they form a growing singly-linked list.
    // Each block's data storage is allocated inline and remains stable
    // until ~SegmentedMPMCQueue frees the entire chain.
    struct Block {
        alignas(CL) std::atomic<uint64_t> sequences[BlockSize];
        alignas(CL) unsigned char data[BlockSize * sizeof(T)];

        // Next block in the chain (nullptr if tail).
        // Set exactly once under the mutex; thereafter read lock-free
        // by producers/consumers traversing the chain.
        std::atomic<Block*> next{nullptr};

        uint64_t base_ticket;

        explicit Block(uint64_t base) noexcept : base_ticket(base) {
            for (size_t i = 0; i < BlockSize; ++i)
                sequences[i].store(base + i, std::memory_order_relaxed);
        }

        ~Block() noexcept {
            for (size_t i = 0; i < BlockSize; ++i) {
                uint64_t seq = sequences[i].load(std::memory_order_relaxed);
                uint64_t written = base_ticket + i + BlockSize;
                if (seq < written)
                    continue;  // never written
                uint64_t consumed = base_ticket + i + 2 * BlockSize;
                if (seq < consumed) {
                    // Written but not consumed
                    std::launder(reinterpret_cast<T*>(
                        data + i * sizeof(T)))->~T();
                }
                // consumed: already destroyed by pop(), skip
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
    SegmentedMPMCQueue()
        : root_block_(new Block(0))
    {
        blocks_.emplace_back(root_block_);
        head_block_.store(root_block_, std::memory_order_relaxed);
        tail_block_.store(root_block_, std::memory_order_relaxed);
    }

    ~SegmentedMPMCQueue() noexcept {
        // All blocks are owned by blocks_ and will be destroyed by the
        // unique_ptr<Block> destructors, which call ~Block() to destruct
        // any remaining live elements.
    }

    SegmentedMPMCQueue(const SegmentedMPMCQueue&) = delete;
    SegmentedMPMCQueue& operator=(const SegmentedMPMCQueue&) = delete;

    // ─── Producer ─────────────────────────────────────────────────────
    // Push a value. Never drops — grows the block chain if necessary.
    // Throws std::bad_alloc on memory exhaustion.
    void push(T value) {
        uint64_t pos = enqueue_pos_.fetch_add(1, std::memory_order_relaxed);

        Block* block = tail_block_.load(std::memory_order_acquire);

        // Find the block that owns `pos`.
        // Start from tail, or fall back to root if tail is ahead.
        if (pos < block->base_ticket) [[unlikely]] {
            block = root_block_;
        }
        while (pos >= block->base_ticket + BlockSize) [[unlikely]] {
            Block* next = block->next.load(std::memory_order_acquire);

            if (!next) {
                // Need to allocate. Double-checked locking.
                std::lock_guard<std::mutex> lock(block_mtx_);

                next = block->next.load(std::memory_order_acquire);
                if (!next) {
                    auto* raw = new Block(block->base_ticket + BlockSize);
                    block->next.store(raw, std::memory_order_release);
                    blocks_.emplace_back(raw);
                    next = raw;
                }
                // Update tail hint after the link is visible.
                tail_block_.store(next, std::memory_order_release);
            } else {
                // Follow existing next. Best-effort tail hint update.
                tail_block_.compare_exchange_weak(block, next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed);
            }
            block = next;
        }

        // Invariant: block->base_ticket <= pos < block->base_ticket + BlockSize
        if (pos < block->base_ticket) [[unlikely]] {
            // tail hint or traversal gave us a block past our position.
            // Linear scan from root (common case: slow producer, blocks consumed).
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize) {
                block = block->next.load(std::memory_order_acquire);
            }
        }

        // Slot inside the current block
        size_t idx = static_cast<size_t>(pos - block->base_ticket);

        // Spin until the slot is free (normally immediate — the block was
        // just allocated or the slot's sequence exactly matches pos).
        while (block->sequences[idx].load(std::memory_order_acquire) != pos) {
            std::this_thread::yield();
        }

        ::new (block->slot_at(idx)) T(std::move(value));

        // Release: make the data write visible to consumers.
        block->sequences[idx].store(pos + BlockSize,
                                    std::memory_order_release);
    }

    // ─── Consumer ─────────────────────────────────────────────────────
    // Pop a value into `out`. Returns false if the queue is empty.
    bool try_pop(T& out) {
        uint64_t pos;

        // 1. Claim a global dequeue position
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

        // 2. Find the block containing `pos`
        Block* block = head_block_.load(std::memory_order_acquire);

        while (pos >= block->base_ticket + BlockSize) {
            Block* next = block->next.load(std::memory_order_acquire);
            if (!next) {
                // The element was enqueued (we checked enq_pos above),
                // so a block MUST exist. Producer is still linking it.
                std::this_thread::yield();
                continue;
            }
            // Best-effort head hint advance (harmless if CAS fails —
            // the fallback check after the loop corrects any drift).
            head_block_.compare_exchange_weak(block, next,
                std::memory_order_release,
                std::memory_order_relaxed);
            block = next;
        }

        // Invariant: block->base_ticket <= pos < block->base_ticket + BlockSize
        if (pos < block->base_ticket) [[unlikely]] {
            // head hint or traversal landed us past our block.
            // Linear scan from root.
            block = root_block_;
            while (pos >= block->base_ticket + BlockSize) {
                block = block->next.load(std::memory_order_acquire);
            }
        }

        // 3. Wait for the producer to finish writing
        size_t idx = static_cast<size_t>(pos - block->base_ticket);

        while (block->sequences[idx].load(std::memory_order_acquire) !=
               pos + BlockSize) {
            std::this_thread::yield();
        }

        // 4. Move data out and destroy
        T* slot = block->slot_at(idx);
        out = std::move(*slot);
        slot->~T();

        // 5. Mark consumed (for the destructor's benefit).
        block->sequences[idx].store(pos + 2 * BlockSize,
                                    std::memory_order_relaxed);

        // 6. Advance head hint if this was the last slot in the block
        if (idx == BlockSize - 1) {
            Block* next = block->next.load(std::memory_order_acquire);
            if (next) {
                // Best-effort advance; harmless if CAS fails (another
                // consumer already advanced it).
                Block* expected = block;
                head_block_.compare_exchange_strong(expected, next,
                    std::memory_order_release);
            }
        }

        return true;
    }

    // ─── Observers ────────────────────────────────────────────────────

    /// Approximate number of elements in the queue.
    /// Under concurrent push/pop this is a point-in-time estimate.
    size_t size() const noexcept {
        uint64_t w = enqueue_pos_.load(std::memory_order_acquire);
        uint64_t r = dequeue_pos_.load(std::memory_order_relaxed);
        return w > r ? static_cast<size_t>(w - r) : 0;
    }

    bool empty() const noexcept { return size() == 0; }

private:
    // ── Producer cache line ──
    alignas(CL) std::atomic<uint64_t> enqueue_pos_{0};

    // ── Consumer cache line ──
    alignas(CL) std::atomic<uint64_t> dequeue_pos_{0};

    // ── Head block (consumer side) ──
    // Points to the block containing the earliest unconsumed element.
    // Consumers may advance this; producers read it but don't modify.
    alignas(CL) std::atomic<Block*> head_block_;

    // ── Tail block (producer side) ──
    // Points to the block most recently appended to the chain.
    // Producers may advance this; consumers read it but don't modify.
    alignas(CL) std::atomic<Block*> tail_block_;

    // ── Root block ──
    // The first block (base_ticket = 0), never changes.
    // Used as a safe starting point for linear scans.
    Block* const root_block_;

    // ── Block management ──
    // Mutex protects block allocation + linking.
    // blocks_ owns all Block objects (stable for lifetime of queue).
    std::mutex block_mtx_;
    std::vector<std::unique_ptr<Block>> blocks_;
};
