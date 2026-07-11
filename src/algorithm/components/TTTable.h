#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

/// Fixed-size hash table for IDA* best_g tracking.
///
/// Stores {hash ↦ g}. Entries are never invalidated — a state reached
/// with a cheaper g replaces any previous entry for the same hash.
class TTTable {
public:
    static constexpr uint32_t BUCKETS = 1u << 20;  // ~1M entries

    struct Entry {
        size_t  hash = 0;
        int32_t g    = 0;
        bool    occupied = false;
    };

    TTTable() : _table(BUCKETS) {}

    /// Lookup: returns stored g if hit, nullptr otherwise.
    const int32_t* lookup(size_t hash) const noexcept {
        for (size_t i = hash & (BUCKETS - 1); ; i = (i + 1) & (BUCKETS - 1)) {
            if (!_table[i].occupied) return nullptr;
            if (_table[i].hash == hash) return &_table[i].g;
        }
    }

    /// Insert or update: stores hash → g if g is cheaper.
    void store(size_t hash, int32_t g) noexcept {
        for (size_t i = hash & (BUCKETS - 1); ; i = (i + 1) & (BUCKETS - 1)) {
            auto& e = _table[i];
            if (!e.occupied) {
                e.hash = hash;
                e.g = g;
                e.occupied = true;
                return;
            }
            if (e.hash == hash) {
                if (g < e.g) e.g = g;
                return;
            }
        }
    }

    void clear() noexcept {
        _table.assign(BUCKETS, Entry{});
    }

private:
    std::vector<Entry> _table;
};
