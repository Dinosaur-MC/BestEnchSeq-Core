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

// ─── EnchSet (inline-only) ─────────────────────────────────────────────

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    Ench *d = reinterpret_cast<Ench *>(_buf);
    auto it = std::lower_bound(d, d + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != d + _size && it->id == id) ? it : d + _size;
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    const Ench *d = reinterpret_cast<const Ench *>(_buf);
    auto it       = std::lower_bound(d, d + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != d + _size && it->id == id) ? it : d + _size;
}

bool EnchSet::contains(int16_t id) const noexcept {
    const Ench *d = reinterpret_cast<const Ench *>(_buf);
    auto it       = std::lower_bound(d, d + _size, id, [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != d + _size && it->id == id);
}

void EnchSet::insert(const Ench &ench) {
    _hash_cache = 0;
    Ench *d     = reinterpret_cast<Ench *>(_buf);
    auto it    = std::lower_bound(d, d + _size, ench.id, [](const Ench &e, int16_t id) { return e.id < id; });
    size_t pos = static_cast<size_t>(it - d);

    if (it != d + _size && it->id == ench.id) {
        d[pos].level = ench.level; // update existing
        return;
    }

    // Guard against overflow — INLINE_N covers all MC use cases.
    if (_size >= INLINE_N) [[unlikely]]
        return;

    // Shift tail to make room, then insert.
    for (size_t i = _size; i > pos; --i)
        d[i] = d[i - 1];
    d[pos] = ench;
    ++_size;
}

void EnchSet::sort() {
    _hash_cache = 0;
    Ench *d     = reinterpret_cast<Ench *>(_buf);
    std::sort(d, d + _size, [](const Ench &a, const Ench &b) { return a.id < b.id; });
}

// ─── EnchSet: lazy hash, serialization ────────────────────────────────

size_t EnchSet::hash() const noexcept {
    if (_hash_cache == 0 && _size > 0) {
        size_t h      = _size;
        const Ench *d = reinterpret_cast<const Ench *>(_buf);
        for (size_t i = 0; i < _size; ++i)
            hash_combine(h, static_cast<size_t>(d[i].id) ^ (static_cast<size_t>(d[i].level) << 16));
        _hash_cache = h;
    }
    return _hash_cache;
}

void EnchSet::serialize(ByteStreamWriter &w) const noexcept {
    w << static_cast<uint64_t>(_size);
    const Ench *d = reinterpret_cast<const Ench *>(_buf);
    for (size_t i = 0; i < _size; ++i)
        w << d[i];
}

void EnchSet::deserialize(ByteStreamReader &r) noexcept {
    clear();
    uint64_t n;
    r >> n;
    if (n > INLINE_N || !r.ok()) {
        r.set_fail();
        return;
    }
    Ench *d = reinterpret_cast<Ench *>(_buf);
    for (uint64_t i = 0; i < n; ++i) {
        r >> d[i];
        if (!r.ok())
            return;
    }
    _size = static_cast<uint8_t>(n);
}

} // namespace algorithm
