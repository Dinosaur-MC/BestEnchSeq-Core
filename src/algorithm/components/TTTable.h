#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

/// Fixed-size hash table for IDA* best_g tracking, epoch-based clear.
///
/// Stores {hash ↦ g}. clear() is O(1) (increment epoch) instead of O(N)
/// memset.  When the table accumulates too many stale entries (>70% of
/// slots occupied by any epoch) the next store triggers a compaction
/// pass that rebuilds only the live entries.
class TTTable {
public:
    static constexpr uint32_t BUCKETS = 1u << 20;      // ~1M entries
    static constexpr float    COMPACT_THRESHOLD = 0.70f;

    struct Entry {
        size_t   hash     = 0;
        int32_t  g        = 0;
        uint32_t epoch    = 0;
        bool     occupied = false;
    };

    TTTable() : _table(BUCKETS) {}

    /// Lookup: returns stored g value pointer if hit, nullptr otherwise.
    const int32_t* lookup(size_t hash) const noexcept {
        for (size_t i = hash & (BUCKETS - 1); ; i = (i + 1) & (BUCKETS - 1)) {
            const auto& e = _table[i];
            if (!e.occupied) return nullptr;
            if (e.epoch == _epoch && e.hash == hash) return &e.g;
            // stale entry or hash collision → keep probing
        }
    }

    /// Insert or update: stores hash → g if g is cheaper.
    void store(size_t hash, int32_t g) noexcept {
        // Compact if too many stale entries degrade probe performance.
        if (_total_occupied > static_cast<size_t>(BUCKETS * COMPACT_THRESHOLD))
            _compact();

        for (size_t i = hash & (BUCKETS - 1); ; i = (i + 1) & (BUCKETS - 1)) {
            auto& e = _table[i];

            // Existing entry from this epoch — update if cheaper.
            if (e.occupied && e.epoch == _epoch) {
                if (e.hash == hash) {
                    if (g < e.g) e.g = g;
                    return;
                }
                continue;   // hash collision — keep probing
            }

            // Empty slot or stale entry — claim it.
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
    }

    /// O(1) clear: increment epoch.  Previous entries become stale and
    /// are skipped during lookup / reclaimed during compaction.
    void clear() noexcept {
        ++_epoch;
        _live_count = 0;
        if (_epoch == 0) [[unlikely]] {
            // uint32_t wrap-around safety — full reset once every 4B clears
            _table.assign(BUCKETS, Entry{});
            _total_occupied = 0;
            _epoch = 1;
        }
    }

    /// Full compaction reset — O(N) but useful between independent
    /// search sessions where epoch-only clear would leave too many
    /// stale entries.
    void clear_and_compact() noexcept {
        _table.assign(BUCKETS, Entry{});
        _total_occupied = 0;
        _live_count = 0;
        _epoch = 1;
    }

private:
    void _compact() {
        // Save live entries (normalize epoch to 1 for the compacted table).
        std::vector<Entry> live;
        live.reserve(_live_count);
        for (auto& e : _table) {
            if (e.occupied && e.epoch == _epoch) {
                e.epoch = 1;
                live.push_back(std::move(e));
            }
        }

        // Full reset, then reinsert live entries.
        _table.assign(BUCKETS, Entry{});
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

    std::vector<Entry> _table;
    uint32_t _epoch          = 1;
    uint32_t _live_count     = 0;
    uint32_t _total_occupied = 0;
};
