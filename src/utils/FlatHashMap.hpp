#pragma once
#include <cstddef>
#include <type_traits>
#include <vector>

/// Open-addressing flat hash map with linear probing.
///
/// Key must be an integral type. All storage is in contiguous vectors —
/// no per-node heap allocations, cache-friendly traversal.
template<typename Key, typename Val>
class FlatHashMap {
    static_assert(std::is_integral_v<Key>, "FlatHashMap: Key must be integral");
public:
    FlatHashMap() = default;

    /// Pre-allocate for at least capacity_hint entries (rounds up to 2^N).
    void reserve(size_t capacity_hint) {
        if (capacity_hint == 0) capacity_hint = 1;
        size_t cap = 1;
        while (cap < capacity_hint) cap <<= 1;
        _mask = cap - 1;
        _keys.assign(cap, Key{});
        _vals.resize(cap);
        _occupied.assign(cap, false);
    }

    /// Lookup. Returns nullptr if key not found.
    Val* find(Key k) noexcept {
        if (_occupied.empty()) return nullptr;
        for (size_t i = k & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) return nullptr;
            if (_keys[i] == k) return &_vals[i];
        }
    }

    const Val* find(Key k) const noexcept {
        if (_occupied.empty()) return nullptr;
        for (size_t i = k & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) return nullptr;
            if (_keys[i] == k) return &_vals[i];
        }
    }

    /// Insert-or-assign. Returns reference to the value slot.
    Val& operator[](Key k) {
        if (_occupied.empty()) reserve(64);
        for (size_t i = k & _mask; ; i = (i + 1) & _mask) {
            if (!_occupied[i]) {
                _keys[i] = k;
                _occupied[i] = true;
                ++_size;
                return _vals[i];
            }
            if (_keys[i] == k) return _vals[i];
        }
    }

    size_t size() const noexcept { return _size; }
    bool   empty() const noexcept { return _size == 0; }

    void clear() {
        if (!_occupied.empty())
            _occupied.assign(_occupied.size(), false);
        _size = 0;
    }

private:
    std::vector<Key>      _keys;
    std::vector<Val>      _vals;
    std::vector<bool>     _occupied;   // bit-compacted occupancy flags
    size_t                _mask  = 0;
    size_t                _size  = 0;
};
