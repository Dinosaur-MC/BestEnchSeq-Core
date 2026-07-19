#pragma once

#include "IQueue.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

// ─── MPSCQueue ───
// Multi-Producer, Single-Consumer lock-free UNBOUNDED queue.
//
// Uses a singly-linked list with a mutex-protected free list for node reuse.
// Producers compete via atomic exchange on the head pointer; the single
// consumer reads from the tail with zero contention.  After steady state
// the free list avoids all dynamic memory allocations.
//
// The queue itself is fully lock-free (exchange + release-store on the
// linked list).  The free list uses a lightweight mutex because a
// lock-free Treiber stack is susceptible to ABA under multi-producer pop
// and single-consumer push — the mutex eliminates that issue with
// negligible contention (critical section is a pointer swap).
//
// try_push() always returns true (unbounded capacity).
//
// Inherits from IQueue<T> for runtime-polymorphic access.
//
// Thread safety:
//   - Multiple writers:  try_push() / try_emplace()
//   - One reader:        try_pop() / clear()
//   - No CAS loops on the consumer path
//
// Requirements:
//   T must be nothrow-destructible (for safe drain on clear/destroy).

template <typename T>
class MPSCQueue final : public IQueue<T> {
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T must be nothrow destructible");

    static constexpr size_t CL = 64;

    // ── Node ──────────────────────────────────────────────────────────
    // Cache-line aligned to prevent false sharing when producers contend
    // on nearby nodes.
    struct alignas(CL) Node {
        alignas(T) std::byte storage[sizeof(T)];
        std::atomic<Node*> next{nullptr};

        T* data_ptr() noexcept {
            return std::launder(reinterpret_cast<T*>(storage));
        }
    };

    // ── Mutex-protected free list ────────────────────────────────────
    // Consumer returns exhausted nodes here; producers re-acquire them.
    // A mutex is used instead of a lock-free Treiber stack to avoid the
    // ABA problem under multi-producer pop + single-consumer push.
    // Contention is negligible — the critical section is ~3 pointer swaps.
    struct alignas(CL) FreeList {
        std::mutex mtx;
        Node* head{nullptr};

        void push(Node* node) noexcept {
            std::lock_guard<std::mutex> lock(mtx);
            node->next.store(head, std::memory_order_relaxed);
            head = node;
        }

        Node* pop() noexcept {
            std::lock_guard<std::mutex> lock(mtx);
            if (!head) return nullptr;
            Node* node = head;
            head = node->next.load(std::memory_order_relaxed);
            node->next.store(nullptr, std::memory_order_relaxed);
            return node;
        }
    };

public:
    using value_type        = T;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;

    // ─── Construction ─────────────────────────────────────────────────

    MPSCQueue() noexcept
        : head_(&dummy_)
        , tail_(&dummy_)
        , size_(0) {}

    ~MPSCQueue() noexcept final {
        clear();
        // dummy_ is embedded — its storage is never constructed, so no ~T()
    }

    MPSCQueue(const MPSCQueue&)            = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    // ─── Producer API (multi-thread safe) ─────────────────────────────

    bool try_push(const T& value) noexcept override final {
        if constexpr (std::is_copy_constructible_v<T>) {
            Node* node = acquire_node();
            ::new (node->data_ptr()) T(value);
            return enqueue_node(node);
        }
        return false;
    }

    bool try_push(T&& value) noexcept override final {
        Node* node = acquire_node();
        ::new (node->data_ptr()) T(std::move(value));
        return enqueue_node(node);
    }

    /// Emplace a T in-place from constructor arguments.
    /// Non-virtual extension (not on IQueue).
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_emplace(Args&&... args) noexcept {
        Node* node = acquire_node();
        ::new (node->data_ptr()) T(std::forward<Args>(args)...);
        return enqueue_node(node);
    }

    // ─── Consumer API (single thread only) ────────────────────────────

    bool try_pop(T& out) noexcept override final {
        Node* next = tail_->next.load(std::memory_order_acquire);
        if (next == nullptr)
            return false;

        // Move data to caller, destruct in-place
        out = std::move(*next->data_ptr());
        next->data_ptr()->~T();

        // Advance tail and recycle old node
        Node* old_tail = tail_;
        tail_ = next;

        if (old_tail != &dummy_)
            free_list_.push(old_tail);

        size_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    // ─── Observers ───────────────────────────────────────────────────

    /// Approximate element count (lower bound under concurrent push).
    size_t size() const noexcept override final {
        return size_.load(std::memory_order_relaxed);
    }

    bool empty() const noexcept override final {
        return size() == 0;
    }

    /// Returns 0 (unbounded — no fixed capacity limit).
    size_t capacity() const noexcept override final { return 0; }

    /// Remove all elements.  Does not require T to be default-constructible.
    void clear() noexcept override final {
        for (;;) {
            Node* next = tail_->next.load(std::memory_order_acquire);
            if (next == nullptr)
                break;

            next->data_ptr()->~T();

            Node* old_tail = tail_;
            tail_ = next;

            if (old_tail != &dummy_)
                free_list_.push(old_tail);

            size_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

private:
    // ── Node lifecycle ────────────────────────────────────────────────

    /// Obtain a node: prefer the free list, fall back to heap allocation.
    Node* acquire_node() noexcept {
        if (Node* node = free_list_.pop()) {
            node->next.store(nullptr, std::memory_order_relaxed);
            return node;
        }
        return new (std::nothrow) Node;
    }

    /// Link the new node into the list and make it visible to the consumer.
    bool enqueue_node(Node* node) noexcept {
        // Atomically swing head_ to the new node, receiving the previous head.
        Node* prev = head_.exchange(node, std::memory_order_acq_rel);
        // Publish the new node to the consumer by linking from the old head.
        prev->next.store(node, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // ── Member variables (each on its own cache line) ─────────────────

    alignas(CL) std::atomic<Node*> head_;   // producers compete here
    alignas(CL) Node* tail_;                // consumer only — no atomic
    alignas(CL) std::atomic<size_t> size_;  // approximate count
    alignas(CL) FreeList free_list_;        // node recycling pool
    Node dummy_;                            // sentinel (no data, ~Node only)
};
