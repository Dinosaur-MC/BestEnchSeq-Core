#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

/// Fixed-size hash table for IDA* best_g tracking, epoch-based clear.
///
/// ## Memory
///
/// The table is NOT allocated in the constructor — allocation is deferred to
/// the first store() call.  A TTTable that is constructed but never used
/// allocates zero heap memory.  Call clear() between independent search
/// sessions to avoid re-populating (the epoch-based O(1) clear retains the
/// buffer).
///
/// ## Hard limits
///
///   BUCKETS   1 048 576  (2²⁰)  —  entries in the open-addressing table
///   ENTRY     24 bytes           —  hash (8) + g (4) + epoch (4) + occupied (1) + padding (7)
///   PEAK      25.2 MiB           —  _table at full occupancy (constexpr: kMaxMemoryBytes)
///   COMPACT   70% stale          —  triggers compaction pass on next store()
///
/// ## Compile-time checks
///
///   BUCKETS must be a power of two  (for cheap modulo via & mask)
///   PEAK_BYTES < 256 MiB            (compile-time assert in class body)

class TTTable {
public:
    static constexpr uint32_t BUCKETS   = 1u << 20;            // 1 048 576 entries
    static constexpr float    COMPACT_THRESHOLD = 0.70f;

    struct TTEntry {
        size_t   hash     = 0;
        int32_t  g        = 0;
        uint32_t epoch    = 0;
        bool     occupied = false;
    };

    // Hard limits (constexpr after TTEntry is complete)
    static constexpr size_t PEAK_BYTES =
        static_cast<size_t>(BUCKETS) * sizeof(TTEntry);       // 25 165 824 B
    static_assert(PEAK_BYTES < (256ull << 20),
        "TTTable: PEAK_BYTES exceeds 256 MiB — review BUCKETS before increasing");
    static_assert((BUCKETS & (BUCKETS - 1)) == 0,
        "TTTable: BUCKETS must be a power of two 2²⁰");
    static constexpr size_t ENTRY_SIZE = sizeof(TTEntry);     // 24 B

    TTTable() noexcept = default;          // no heap: table is lazily allocated
    ~TTTable() noexcept = default;
    TTTable(TTTable &&) noexcept = default;
    TTTable &operator=(TTTable &&) noexcept = default;

    // Non-copyable (large internal buffer).
    TTTable(const TTTable &)            = delete;
    TTTable &operator=(const TTTable &) = delete;

    /// Lookup: returns stored g value pointer if hit, nullptr otherwise.
    /// Returns nullptr when the table hasn't been allocated yet (no prior
    /// store() call).
    const int32_t* lookup(size_t hash) const noexcept {
        if (_table.empty())
            return nullptr;

        for (size_t i = hash & (BUCKETS - 1), probe = 0;
             probe < BUCKETS;
             i = (i + 1) & (BUCKETS - 1), ++probe) {
            const auto& e = _table[i];
            if (!e.occupied) return nullptr;
            if (e.epoch == _epoch && e.hash == hash) return &e.g;
        }
        return nullptr;
    }

    /// Insert or update: stores hash → g if g is cheaper than existing.
    ///
    /// First call allocates the internal table (~25 MiB).
    /// Subsequent calls operate on the pre-allocated buffer.
    void store(size_t hash, int32_t g) noexcept {
        // Lazily allocate on first access.
        if (_table.empty())
            _table.resize(BUCKETS);

        // Compact if too many stale entries degrade probe performance.
        if (_total_occupied > static_cast<size_t>(BUCKETS * COMPACT_THRESHOLD))
            _compact();

        for (size_t i = hash & (BUCKETS - 1), probe = 0;
             probe < BUCKETS;
             i = (i + 1) & (BUCKETS - 1), ++probe) {
            auto& e = _table[i];

            if (e.occupied && e.epoch == _epoch) {
                if (e.hash == hash) {
                    if (g < e.g) e.g = g;
                    return;
                }
                continue;
            }

            bool was_empty = !e.occupied;
            e.hash = hash;
            e.g    = g;
            e.epoch = _epoch;
            if (was_empty) {
                e.occupied = true;
                ++_total_occupied;
            }
            ++_live_count;
            return;
        }

        // All slots occupied or all stale — force compaction and retry.
        // const_cast is safe: _compact modifies epoch/occupancy but not
        // the logical hash-payload mapping.
        const_cast<TTTable*>(this)->_compact();
        for (size_t i = hash & (BUCKETS - 1), probe = 0;
             probe < BUCKETS;
             i = (i + 1) & (BUCKETS - 1), ++probe) {
            auto& e = _table[i];
            if (!e.occupied) {
                e.hash = hash;
                e.g = g;
                e.epoch = _epoch;
                e.occupied = true;
                ++_total_occupied;
                ++_live_count;
                return;
            }
        }
    }

    /// O(1) clear: increment epoch.  Previous entries become stale and
    /// skipped during lookup / reclaimed during compaction.
    void clear() noexcept {
        ++_epoch;
        _live_count = 0;
        if (_epoch == 0) [[unlikely]] {
            // uint32_t wrap-around — full reset every ~4B clears.
            if (!_table.empty())
                _table.assign(BUCKETS, TTEntry{});
            _total_occupied = 0;
            _epoch = 1;
        }
    }

    /// Full compaction reset — O(N).  Use between independent search
    /// sessions where epoch-only clear would leave too many stale entries.
    void clear_and_compact() noexcept {
        if (_table.empty()) {
            _epoch = 1;
            _live_count = 0;
            _total_occupied = 0;
            return;
        }
        _table.assign(BUCKETS, TTEntry{});
        _total_occupied = 0;
        _live_count = 0;
        _epoch = 1;
    }

    /// Memory metrics (for diagnostics).
    size_t allocated_bytes() const noexcept {
        return _table.empty() ? 0 : _table.capacity() * sizeof(TTEntry);
    }
    uint32_t live_entries() const noexcept { return _live_count; }
    uint32_t total_occupied() const noexcept { return _total_occupied; }

private:
    void _compact() {
        if (_table.empty()) return;

        // Save live entries (normalize epoch to 1 for the compacted table).
        std::vector<TTEntry> live;
        live.reserve(_live_count);
        for (auto& e : _table) {
            if (e.occupied && e.epoch == _epoch) {
                e.epoch = 1;
                live.push_back(std::move(e));
            }
        }

        // Full reset, then reinsert live entries.
        _table.assign(BUCKETS, TTEntry{});
        for (auto& e : live) {
            for (size_t i = e.hash & (BUCKETS - 1); ; i = (i + 1) & (BUCKETS - 1)) {
                auto& slot = _table[i];
                if (!slot.occupied) {
                    slot = std::move(e);
                    break;
                }
            }
        }

        _epoch = 1;
        _live_count = static_cast<uint32_t>(live.size());
        _total_occupied = static_cast<uint32_t>(live.size());
    }

    std::vector<TTEntry> _table;        // empty until first store() call
    uint32_t _epoch          = 1;
    uint32_t _live_count     = 0;
    uint32_t _total_occupied = 0;
};
