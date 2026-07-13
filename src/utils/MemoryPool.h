#pragma once
#include <cstddef>
#include <memory_resource>

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
            std::pmr::new_delete_resource()) noexcept;

    ~MemoryPool() override;

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    MemoryPool(MemoryPool&& other) noexcept;
    MemoryPool& operator=(MemoryPool&& other) noexcept;

    /// Release all pool memory.  Chunks are cached internally; subsequent
    /// allocations reuse them without upstream calls.
    void release() noexcept;

    /// Release all pool memory and free cached chunks upstream.
    /// Use only when the pool will be idle for a long period (e.g. thread
    /// destruction).
    void release_all() noexcept;

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

    void* do_allocate(size_t bytes, size_t alignment) override;
    void  do_deallocate(void* p, size_t bytes, size_t alignment) noexcept override;
    bool  do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    void _free_chunk_chain(Chunk* head) noexcept;

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
