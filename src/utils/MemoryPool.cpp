#include "MemoryPool.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

MemoryPool::MemoryPool(size_t initial_chunk_size,
                       std::pmr::memory_resource* upstream) noexcept
    : _upstream(upstream)
    , _chunk_size(std::max(initial_chunk_size, sizeof(Chunk) + 64))
{}

MemoryPool::~MemoryPool() {
    release_all();
}

MemoryPool::MemoryPool(MemoryPool&& other) noexcept
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

MemoryPool& MemoryPool::operator=(MemoryPool&& other) noexcept {
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

void MemoryPool::release() noexcept {
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

void MemoryPool::release_all() noexcept {
    release();
    _free_chunk_chain(_cached);
    _cached = nullptr;
    _cached_count = 0;
    _chunk_size = kDefaultChunkSize;
}

void* MemoryPool::do_allocate(size_t bytes, size_t alignment) {
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
    if (!std::align(alignment, bytes, aligned, align_space)) {
        // Should never happen: chunk is at least bytes + alignment bytes
        std::abort();
    }
    _cur_pos = static_cast<std::byte*>(aligned) + bytes;
    _end_pos = static_cast<std::byte*>(data_start) + data_size;
    _total_allocated += bytes;
    return aligned;
}

void MemoryPool::do_deallocate(void*, size_t, size_t) noexcept {
    // Monotonic — no-op.  All memory reclaimed via release() / destructor.
}

bool MemoryPool::do_is_equal(
    const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

void MemoryPool::_free_chunk_chain(Chunk* head) noexcept {
    while (head) {
        Chunk* next = head->next;
        _upstream->deallocate(head, sizeof(Chunk) + head->capacity, alignof(Chunk));
        head = next;
    }
}
