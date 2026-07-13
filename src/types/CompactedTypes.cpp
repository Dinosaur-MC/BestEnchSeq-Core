#include "CompactedTypes.h"
#include <algorithm>

namespace compact {

bool EnchInfo::is_conflict(const EnchInfo &other) const noexcept {
    if (exc_mask.size() != other.exc_mask.size())
        return true;
    const size_t n = exc_mask.size();
    const MaskType *p = exc_mask.data();
    const MaskType *q = other.exc_mask.data();
    for (size_t i = 0; i < n; ++i) {
        if (p[i] & q[i]) [[unlikely]]
            return true;
    }
    return false;
}

// ─── EnchSet ────────────────────────────────────────────────────────────────

void EnchSet::_migrate_to_heap(size_t new_cap) {
    std::vector<Ench> tmp;
    tmp.reserve(new_cap);
    Ench *src = reinterpret_cast<Ench *>(_buf);
    for (size_t i = 0; i < _size; ++i)
        tmp.push_back(src[i]);
    _mode = 1;
    new (&_vec) std::vector<Ench>(std::move(tmp));
}

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    Ench *d = _data();
    auto it = std::lower_bound(d, d + _size, id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != d + _size && it->id == id) ? it : d + _size;
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    const Ench *d = _data();
    auto it = std::lower_bound(d, d + _size, id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != d + _size && it->id == id) ? it : d + _size;
}

bool EnchSet::contains(int16_t id) const noexcept {
    return find(id) != end();
}

void EnchSet::insert(const Ench &ench) {
    Ench *d = _data();

    // ── Insert or update ──
    auto it = std::lower_bound(d, d + _size, ench.id,
        [](const Ench &e, int16_t id) { return e.id < id; });

    size_t pos = static_cast<size_t>(it - d);

    if (it != d + _size && it->id == ench.id) {
        // Update existing
        d[pos].level = ench.level;
        return;
    }

    // Insert new — need space
    if (pos >= _size) pos = _size;  // append

    size_t needed = _size + 1;
    if (_is_inline() && needed > INLINE_N) {
        // Switch to heap: copy current data, insert new element
        std::vector<Ench> tmp;
        tmp.reserve(needed + 2);
        for (size_t i = 0; i < _size; ++i)
            tmp.push_back(d[i]);
        tmp.insert(tmp.begin() + static_cast<ptrdiff_t>(pos), ench);
        _mode = 1;
        new (&_vec) std::vector<Ench>(std::move(tmp));
    } else if (!_is_inline()) {
        _vec.insert(_vec.begin() + static_cast<ptrdiff_t>(pos), ench);
    } else {
        // Inline insert — shift tail
        for (size_t i = _size; i > pos; --i)
            d[i] = d[i - 1];
        d[pos] = ench;
    }
    ++_size;
}

void EnchSet::sort() {
    Ench *d = _data();
    std::sort(d, d + _size,
              [](const Ench &a, const Ench &b) { return a.id < b.id; });
}

} // namespace compact
