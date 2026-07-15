#pragma once
#include <cstddef>
#include <type_traits>
#include <vector>

/// Open-addressing flat hash map with linear probing and auto-growth.
///
/// Key must be an integral type. All storage is in contiguous vectors —
/// no per-node heap allocations, cache-friendly traversal.
///
/// The table auto-grows when load factor exceeds ~75%: capacity doubles,
/// then all live entries are rehashed into the new table.  An explicit
/// reserve() is still available for pre-allocation when the eventual
/// size is known upfront.
template<typename Key, typename Val>
class FlatHashMap {
    static_assert(std::is_integral_v<Key>, "FlatHashMap: Key must be integral");
public:
    FlatHashMap() = default;

    /// Pre-allocate for at least capacity_hint entries (rounds up to 2^N).
    /// Safe to call multiple times — no-op if already large enough.
    void reserve(size_t capacity_hint) {
        if (capacity_hint == 0) capacity_hint = 1;
        size_t cap = 1;
        while (cap < capacity_hint) cap <<= 1;
        if (cap <= _mask + 1 && !_occupied.empty())
            return;     // already large enough
        _rehash(cap);
    }

    /// Lookup. Returns nullptr if key not found.
    Val* find(Key k) noexcept {
        if (_occupied.empty()) return nullptr;
        for (size_t i = static_cast<size_t>(k) & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) return nullptr;
            if (_keys[i] == k) return &_vals[i];
        }
    }

    const Val* find(Key k) const noexcept {
        if (_occupied.empty()) return nullptr;
        for (size_t i = static_cast<size_t>(k) & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) return nullptr;
            if (_keys[i] == k) return &_vals[i];
        }
    }

    /// Insert-or-assign. Auto-grows if load factor exceeds ~75%.
    /// Returns reference to the value slot.
    Val& operator[](Key k) {
        if (_occupied.empty()) {
            reserve(64);        // sensible default initial capacity
        }

        // Auto-grow when load factor > 75%
        if (_size * 4 > (_mask + 1) * 3)
            _rehash((_mask + 1) * 2);

        for (size_t i = static_cast<size_t>(k) & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) {
                _keys[i] = k;
                _occupied[i] = true;
                ++_size;
                return _vals[i];
            }
            if (_keys[i] == k) return _vals[i];
        }
    }

    size_t size()  const noexcept { return _size; }
    bool   empty() const noexcept { return _size == 0; }

    /// Bucket iteration for serialization.
    size_t bucket_count() const noexcept { return _occupied.size(); }
    bool occupied_at(size_t i) const noexcept { return i < _occupied.size() && _occupied[i]; }
    Key key_at(size_t i) const noexcept { return _keys[i]; }
    Val& val_at(size_t i) noexcept { return _vals[i]; }
    const Val& val_at(size_t i) const noexcept { return _vals[i]; }

    void clear() {
        if (!_occupied.empty())
            _occupied.assign(_occupied.size(), false);
        _size = 0;
    }

private:
    /// Rebuild the table with a new capacity (must be power of two).
    void _rehash(size_t new_cap) {
        auto old_keys     = std::move(_keys);
        auto old_vals     = std::move(_vals);
        auto old_occupied = std::move(_occupied);
        size_t old_mask   = _mask;

        _mask = new_cap - 1;
        _keys.assign(new_cap, Key{});
        _vals.resize(new_cap);
        _occupied.assign(new_cap, false);
        _size = 0;

        // Guard: moved-from vector may be empty on first call.
        if (!old_occupied.empty()) {
        for (size_t i = 0; i <= old_mask; ++i) {
            if (old_occupied[i]) {
                Key k = old_keys[i];
                for (size_t j = static_cast<size_t>(k) & _mask; ; j = (j + 1) & _mask) {
                    if (!_occupied[j]) {
                        _keys[j] = k;
                        _vals[j] = std::move(old_vals[i]);
                        _occupied[j] = true;
                        ++_size;
                        break;
                    }
                }
            }
        }
        }  // if (!old_occupied.empty())
    }

    std::vector<Key>      _keys;
    std::vector<Val>      _vals;
    std::vector<bool>     _occupied;   // bit-compacted occupancy flags
    size_t                _mask  = 0;
    size_t                _size  = 0;
};
