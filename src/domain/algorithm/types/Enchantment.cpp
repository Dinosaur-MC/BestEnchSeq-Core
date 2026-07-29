#include "Enchantment.h"
#include "common/utils/HashUtils.hpp"
#include <algorithm>

namespace algorithm {

bool EnchInfo::is_conflict(const EnchInfo &other) const noexcept {
    if (exc_mask.size() != other.exc_mask.size())
        return true;
    const size_t n    = exc_mask.size();
    const MaskType *p = exc_mask.data();
    const MaskType *q = other.exc_mask.data();
    for (size_t i = 0; i < n; ++i) {
        if (p[i] & q[i]) [[unlikely]]
            return true;
    }
    return false;
}

// ─── EnchSet ─────────────────────────────────────────────

EnchSet::EnchSet(std::initializer_list<Ench> il) noexcept
    : _size(std::min(il.size(), INLINE_N)), _hash_cache(0) {
    std::memcpy(_buf, il.begin(), sizeof(Ench) * _size);
    sort();
}
EnchSet::EnchSet(const EnchSet &o) noexcept : _size(o._size), _hash_cache(o._hash_cache) {
    std::memcpy(_buf, o._buf, INLINE_BYTES);
}

EnchSet &EnchSet::operator=(const EnchSet &o) noexcept {
    if (this != &o) {
        _size       = o._size;
        _hash_cache = o._hash_cache;
        std::memcpy(_buf, o._buf, INLINE_BYTES);
    }
    return *this;
}

EnchSet::EnchSet(EnchSet &&o) noexcept : _size(o._size), _hash_cache(o._hash_cache) {
    std::memcpy(_buf, o._buf, INLINE_BYTES);
    o._size       = 0;
    o._hash_cache = 0;
}

EnchSet &EnchSet::operator=(EnchSet &&o) noexcept {
    if (this != &o) {
        _size       = o._size;
        _hash_cache = o._hash_cache;
        std::memcpy(_buf, o._buf, INLINE_BYTES);
        o._size       = 0;
        o._hash_cache = 0;
    }
    return *this;
}

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    auto it = std::lower_bound(_buf, _buf + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != _buf + _size && it->id == id) ? it : _buf + _size;
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    auto it = std::lower_bound(_buf, _buf + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != _buf + _size && it->id == id) ? it : _buf + _size;
}

bool EnchSet::contains(int16_t id) const noexcept {
    auto it = std::lower_bound(_buf, _buf + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != _buf + _size && it->id == id);
}

void EnchSet::insert(const Ench &ench) noexcept {
    _hash_cache = 0;
    auto it =
        std::lower_bound(_buf, _buf + _size, ench.id, [](const Ench &e, int16_t id) { return e.id < id; });
    size_t pos = it - _buf;

    if (it != _buf + _size && it->id == ench.id) {
        _buf[pos].level = ench.level; // update existing
        return;
    }

    // Guard against overflow — INLINE_N covers all MC use cases.
    if (_size >= INLINE_N) [[unlikely]]
        return;

    // Shift tail to make room, then insert.
    for (size_t i = _size; i > pos; --i)
        _buf[i] = _buf[i - 1];
    _buf[pos] = ench;
    ++_size;
}

void EnchSet::clear() noexcept {
    _size       = 0;
    _hash_cache = 0;
}

void EnchSet::sort() noexcept {
    _hash_cache = 0;
    if (_size > INLINE_N) [[unlikely]]
        _size = INLINE_N;
    std::sort(_buf, _buf + _size, [](const Ench &a, const Ench &b) { return a.id < b.id; });
}

// ─── EnchSet: lazy hash, serialization ────────────────────────────────

size_t EnchSet::hash() const noexcept {
    if (_hash_cache == 0 && _size > 0) {
        size_t h = _size;
        for (size_t i = 0; i < _size; ++i)
            hash_combine(h, static_cast<size_t>(_buf[i].id) ^ (static_cast<size_t>(_buf[i].level) << 16));
        _hash_cache = h;
    }
    return _hash_cache;
}

void EnchSet::rehash() const noexcept {
    _hash_cache = 0;
    (void)hash();
}

void EnchSet::serialize(ByteStreamWriter &w) const noexcept {
    w << _size;
    const Ench *d = _buf;
    for (size_t i = 0; i < _size; ++i)
        w << d[i];
}

void EnchSet::deserialize(ByteStreamReader &r) noexcept {
    clear();
    size_t n;
    r >> n;
    if (n > INLINE_N || !r.ok()) {
        r.set_fail();
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        r >> _buf[i];
        if (!r.ok())
            return;
    }
    _size = n;
}

} // namespace algorithm
