#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <concepts>
#include <utility>

template <typename T>
concept Poolable = std::destructible<T> && !std::is_array_v<T>;

/// Fixed-size freelist object pool for type T.
///
/// Manages geometrically-growing blocks of pre-allocated storage.
/// O(1) acquire()/release().  acquire() perfect-forwards construction args.
///
/// Not thread-safe.
template <Poolable T>
class ObjectPool {
public:
    using value_type = T;

    /// @param initial_block_power  log2 of first block size (default 6 = 64)
    explicit ObjectPool(size_t initial_block_power = 6) noexcept
        : _block_power(initial_block_power)
    {}

    ~ObjectPool() { clear(); }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ObjectPool(ObjectPool&& other) noexcept
        : _free_head(std::exchange(other._free_head, nullptr))
        , _blocks(std::exchange(other._blocks, nullptr))
        , _total_slots(std::exchange(other._total_slots, 0))
        , _free_count(std::exchange(other._free_count, 0))
        , _block_power(std::exchange(other._block_power, 6))
    {}

    ObjectPool& operator=(ObjectPool&& other) noexcept {
        if (this != &other) {
            clear();
            _free_head   = std::exchange(other._free_head, nullptr);
            _blocks      = std::exchange(other._blocks, nullptr);
            _total_slots = std::exchange(other._total_slots, 0);
            _free_count  = std::exchange(other._free_count, 0);
            _block_power = std::exchange(other._block_power, 6);
        }
        return *this;
    }

    /// Acquire a constructed T from the pool.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] T* acquire(Args&&... args) {
        if (!_free_head) {
            size_t count = size_t(1) << _block_power;
            if (_block_power < 16) ++_block_power;
            _alloc_block(count);
        }

        Node* node = _free_head;
        _free_head = node->next;
        --_free_count;

        try {
            return std::construct_at(reinterpret_cast<T*>(node), std::forward<Args>(args)...);
        } catch (...) {
            // Construction failed — return node to free list
            node->next = _free_head;
            _free_head = node;
            ++_free_count;
            throw;
        }
    }

    /// Release a T back to the pool.
    void release(T* ptr) noexcept {
        if (!ptr) return;
        std::destroy_at(ptr);
        auto* node = reinterpret_cast<Node*>(ptr);
        node->next = _free_head;
        _free_head = node;
        ++_free_count;
    }

    /// Release all objects and free all blocks.
    void clear() noexcept {
        Block* blk = _blocks;
        while (blk) {
            Block* next = blk->next;
            ::operator delete(blk, std::align_val_t{alignof(Node)});
            blk = next;
        }
        _free_head = nullptr;
        _blocks = nullptr;
        _total_slots = 0;
        _free_count = 0;
        _block_power = 6;
    }

    size_t capacity()  const noexcept { return _total_slots; }
    size_t available() const noexcept { return _free_count; }
    size_t block_count() const noexcept {
        size_t n = 0;
        for (Block* b = _blocks; b; b = b->next) ++n;
        return n;
    }

private:
    union Node {
        Node* next;
        alignas(T) std::byte value[sizeof(T)];
        Node() {}      // no auto-init of value
        ~Node() {}     // lifecycle managed by pool
    };

    struct Block {
        size_t count;
        Block* next;
        // Node array follows immediately after the header
    };

    void _alloc_block(size_t count) {
        size_t total = sizeof(Block) + count * sizeof(Node);
        void* mem = ::operator new(total, std::align_val_t{alignof(Node)});
        auto* blk = static_cast<Block*>(mem);
        blk->count = count;
        blk->next = _blocks;
        _blocks = blk;

        Node* nodes = reinterpret_cast<Node*>(blk + 1);
        // Link into free list (reverse order so first acquire gives first node)
        for (size_t i = count; i > 0; --i) {
            nodes[i - 1].next = _free_head;
            _free_head = &nodes[i - 1];
        }
        _total_slots += count;
        _free_count += count;
    }

    Node*  _free_head   = nullptr;
    Block* _blocks      = nullptr;
    size_t _total_slots = 0;
    size_t _free_count  = 0;
    size_t _block_power;
};
