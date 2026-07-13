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

#ifdef BESQ_USE_ENCHSET_BITMAP

// ── Bitmap implementation ──

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    if (!contains(id)) return _levels.end();
    auto it = std::lower_bound(_levels.begin(), _levels.end(), id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return it;
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    if (!contains(id)) return _levels.end();
    auto it = std::lower_bound(_levels.begin(), _levels.end(), id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return it;
}

bool EnchSet::contains(int16_t id) const noexcept {
    if (id < 0) return false;
    auto idx = static_cast<size_t>(id) / 64;
    auto bit = static_cast<size_t>(id) % 64;
    return idx < _present.size() && (_present[idx] & (1ULL << bit));
}

void EnchSet::insert(const Ench &ench) {
    if (ench.id < 0) return;
    _grow_for(ench.id);
    auto idx = static_cast<size_t>(ench.id) / 64;
    auto bit = static_cast<size_t>(ench.id) % 64;
    uint64_t old = _present[idx];
    _present[idx] |= (1ULL << bit);

    if (old & (1ULL << bit)) {
        // Update existing level
        auto it = std::lower_bound(_levels.begin(), _levels.end(), ench.id,
            [](const Ench &e, int16_t id) { return e.id < id; });
        if (it != _levels.end() && it->id == ench.id)
            it->level = ench.level;
    } else {
        // Insert new
        auto it = std::lower_bound(_levels.begin(), _levels.end(), ench.id,
            [](const Ench &e, int16_t id) { return e.id < id; });
        _levels.insert(it, ench);
    }
}

void EnchSet::sort() {
    std::sort(_levels.begin(), _levels.end(),
              [](const Ench &a, const Ench &b) { return a.id < b.id; });
    // Rebuild bitmask from sorted levels
    _present.assign(_present.size(), 0);
    for (const auto &e : _levels) {
        _grow_for(e.id);
        auto idx = static_cast<size_t>(e.id) / 64;
        auto bit = static_cast<size_t>(e.id) % 64;
        _present[idx] |= (1ULL << bit);
    }
}

#else // !BESQ_USE_ENCHSET_BITMAP

// ── Sorted-vector implementation (default) ──

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    auto it = std::lower_bound(_levels.begin(), _levels.end(), id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != _levels.end() && it->id == id) ? it : _levels.end();
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    auto it = std::lower_bound(_levels.begin(), _levels.end(), id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    return (it != _levels.end() && it->id == id) ? it : _levels.end();
}

bool EnchSet::contains(int16_t id) const noexcept { return find(id) != _levels.end(); }

void EnchSet::insert(const Ench &ench) {
    auto it = std::lower_bound(_levels.begin(), _levels.end(), ench.id,
        [](const Ench &e, int16_t id) { return e.id < id; });
    if (it != _levels.end() && it->id == ench.id) {
        it->level = ench.level; // update existing
    } else {
        _levels.insert(it, ench); // insert at sorted position
    }
}

void EnchSet::sort() {
    std::sort(_levels.begin(), _levels.end(),
              [](const Ench &a, const Ench &b) { return a.id < b.id; });
}

#endif // BESQ_USE_ENCHSET_BITMAP

} // namespace compact
