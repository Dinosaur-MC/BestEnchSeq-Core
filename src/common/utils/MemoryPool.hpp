#pragma once
#include <cstddef>
#include <memory_resource>
#include <utility>

/// Monotonic buffer memory_resource for append-only allocations.
///
/// allocate() bumps a pointer in the current chunk.  When the chunk is
/// exhausted, a new chunk (geometrically larger) is allocated from upstream.
/// deallocate() is a no-op — all memory is reclaimed at once via release()
/// or the destructor.
///
/// Chunks are cached between release() calls for zero-cost reuse.
/// release_all() frees cached chunks when the pool will be idle.
class MemoryPool final : public std::pmr::memory_resource {
public:
    static constexpr size_t kDefaultChunkSize = 65536;
    static constexpr size_t kMaxChunkSize = 1u << 20;   // 1 MiB

    /// @param initial_chunk_size  First-chunk and minimum chunk size.
    /// @param upstream  Backing memory_resource for chunk allocation.
    explicit MemoryPool(
        size_t initial_chunk_size = kDefaultChunkSize,
        std::pmr::memory_resource* upstream =
            std::pmr::new_delete_resource()) noexcept
        : _upstream(upstream)
        , _chunk_size(std::max(initial_chunk_size, sizeof(Chunk) + 64))
    {}

    ~MemoryPool() override { release_all(); }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    MemoryPool(MemoryPool&& other) noexcept
        : _upstream(other._upstream)
        , _cur_chunk(std::exchange(other._cur_chunk, nullptr))
        , _cur_pos(std::exchange(other._cur_pos, nullptr))
        , _end_pos(std::exchange(other._end_pos, nullptr))
        , _chunks(std::exchange(other._chunks, nullptr))
        , _cached(std::exchange(other._cached, nullptr))
        , _cached_count(std::exchange(other._cached_count, 0))
        , _chunk_size(std::exchange(other._chunk_size, kDefaultChunkSize))
        , _total_allocated(std::exchange(other._total_allocated, 0))
        , _total_wasted(std::exchange(other._total_wasted, 0))
        , _chunk_count(std::exchange(other._chunk_count, 0))
    {}

    MemoryPool& operator=(MemoryPool&& other) noexcept {
        if (this != &other) {
            release_all();
            _upstream        = other._upstream;
            _cur_chunk       = std::exchange(other._cur_chunk, nullptr);
            _cur_pos         = std::exchange(other._cur_pos, nullptr);
            _end_pos         = std::exchange(other._end_pos, nullptr);
            _chunks          = std::exchange(other._chunks, nullptr);
            _cached          = std::exchange(other._cached, nullptr);
            _cached_count    = std::exchange(other._cached_count, 0);
            _chunk_size      = std::exchange(other._chunk_size, kDefaultChunkSize);
            _total_allocated = std::exchange(other._total_allocated, 0);
            _total_wasted    = std::exchange(other._total_wasted, 0);
            _chunk_count     = std::exchange(other._chunk_count, 0);
        }
        return *this;
    }

    /// Release all pool memory.  Chunks are cached internally; subsequent
    /// allocations reuse them without upstream calls.
    void release() noexcept {
        // _cur_chunk is always in the _chunks list (added by do_allocate).
        // Null the alias BEFORE moving the list to avoid a double-add.
        _cur_chunk = nullptr;
        _cur_pos = nullptr;
        _end_pos = nullptr;

        // Move all live chunks to the cached list
        if (_chunks) {
            // Find tail of _chunks chain
            Chunk* tail = _chunks;
            while (tail->next) tail = tail->next;
            tail->next = _cached;
            _cached = _chunks;
            _cached_count += _chunk_count;
            _chunks = nullptr;
            _chunk_count = 0;
        }

        _total_allocated = 0;
    }

    /// Release all pool memory and free cached chunks upstream.
    /// Use only when the pool will be idle for a long period (e.g. thread
    /// destruction).
    void release_all() noexcept {
        release();
        _free_chunk_chain(_cached);
        _cached = nullptr;
        _cached_count = 0;
        _chunk_size = kDefaultChunkSize;
    }

    // ── Diagnostics ─────────────────────────────────────────────────────
    size_t total_allocated() const noexcept { return _total_allocated; }
    size_t total_wasted()    const noexcept { return _total_wasted; }
    size_t chunk_count()     const noexcept { return _chunk_count; }
    size_t cached_chunks()   const noexcept { return _cached_count; }

private:
    struct Chunk {
        Chunk*  next;
        size_t  capacity;          // usable bytes after header
        std::byte* data() noexcept { return reinterpret_cast<std::byte*>(this + 1); }
        const std::byte* data() const noexcept { return reinterpret_cast<const std::byte*>(this + 1); }
    };

    void* do_allocate(size_t bytes, size_t alignment) override {
        // Try current chunk first
        if (_cur_chunk) {
            void* aligned = _cur_pos;
            size_t space = static_cast<size_t>(_end_pos - _cur_pos);
            if (std::align(alignment, bytes, aligned, space)) {
                _cur_pos = static_cast<std::byte*>(aligned) + bytes;
                _total_allocated += bytes;
                return aligned;
            }
            // Not enough space — track waste from the tail of this chunk
            _total_wasted += static_cast<size_t>(_end_pos - _cur_pos);
        }

        // Try a cached chunk — re-add to _chunks for lifecycle tracking
        if (_cached) {
            Chunk* ch = _cached;
            _cached = ch->next;
            --_cached_count;

            void* data_start = ch->data();
            size_t capacity = ch->capacity;
            void* aligned = data_start;
            size_t space = capacity;
            if (std::align(alignment, bytes, aligned, space)) {
                // Re-link into _chunks so release() can find it
                ch->next = _chunks;
                _chunks = ch;
                ++_chunk_count;

                _cur_chunk = ch;
                _cur_pos = static_cast<std::byte*>(aligned) + bytes;
                _end_pos = static_cast<std::byte*>(data_start) + capacity;
                _total_allocated += bytes;
                return aligned;
            }
            // Cached chunk somehow too small — free it and fall through
            _upstream->deallocate(ch, sizeof(Chunk) + ch->capacity, alignof(Chunk));
        }

        // Allocate a new chunk from upstream
        size_t data_size = std::max(bytes, _chunk_size);
        size_t alloc_size = sizeof(Chunk) + data_size;

        void* mem = _upstream->allocate(alloc_size, alignof(Chunk));
        auto* ch = static_cast<Chunk*>(mem);
        ch->next = _chunks;
        ch->capacity = data_size;
        _chunks = ch;
        ++_chunk_count;

        // Bump chunk size for next allocation (geometric growth, capped at max)
        _chunk_size = std::min(_chunk_size * 2, kMaxChunkSize);

        _cur_chunk = ch;
        void* data_start = ch->data();
        void* aligned = data_start;
        size_t align_space = data_size;
        if (!std::align(alignment, bytes, aligned, align_space)) [[unlikely]] {
#ifndef NDEBUG
            throw std::bad_alloc();
#else
            std::abort();
#endif
        }
        _cur_pos = static_cast<std::byte*>(aligned) + bytes;
        _end_pos = static_cast<std::byte*>(data_start) + data_size;
        _total_allocated += bytes;
        return aligned;
    }

    void do_deallocate(void*, size_t, size_t) noexcept override {
        // Monotonic — no-op.  All memory reclaimed via release() / destructor.
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    void _free_chunk_chain(Chunk* head) noexcept {
        while (head) {
            Chunk* next = head->next;
            _upstream->deallocate(head, sizeof(Chunk) + head->capacity, alignof(Chunk));
            head = next;
        }
    }

    std::pmr::memory_resource* _upstream;

    Chunk*    _cur_chunk = nullptr;
    std::byte* _cur_pos  = nullptr;
    std::byte* _end_pos  = nullptr;

    Chunk* _chunks       = nullptr;  // all live chunks (for release)
    Chunk* _cached       = nullptr;  // released chunks (for reuse)
    size_t _cached_count = 0;

    size_t _chunk_size;

    size_t _total_allocated = 0;
    size_t _total_wasted    = 0;
    size_t _chunk_count     = 0;
};
