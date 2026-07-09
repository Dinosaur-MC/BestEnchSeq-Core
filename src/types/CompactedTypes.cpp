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

EnchSet::iterator EnchSet::find(int16_t id) noexcept {
    auto it = std::lower_bound(_enchs.begin(), _enchs.end(), id,
        [](const Ench& e, int16_t id) { return e.id < id; });
    return (it != _enchs.end() && it->id == id) ? it : _enchs.end();
}

EnchSet::const_iterator EnchSet::find(int16_t id) const noexcept {
    auto it = std::lower_bound(_enchs.begin(), _enchs.end(), id,
        [](const Ench& e, int16_t id) { return e.id < id; });
    return (it != _enchs.end() && it->id == id) ? it : _enchs.end();
}

bool EnchSet::contains(int16_t id) const noexcept {
    return find(id) != _enchs.end();
}

void EnchSet::insert(Ench ench) {
    auto it = std::lower_bound(_enchs.begin(), _enchs.end(), ench.id,
        [](const Ench& e, int16_t id) { return e.id < id; });
    if (it != _enchs.end() && it->id == ench.id) {
        it->level = ench.level;  // update existing
    } else {
        _enchs.insert(it, ench); // insert at sorted position
    }
}

void EnchSet::merge(const EnchSet& other) {
    for (const auto& e : other._enchs) {
        auto it = std::lower_bound(_enchs.begin(), _enchs.end(), e.id,
            [](const Ench& x, int16_t id) { return x.id < id; });
        if (it != _enchs.end() && it->id == e.id) {
            if (e.level > it->level) it->level = e.level;
        } else {
            _enchs.insert(it, e);
        }
    }
}

} // namespace compact
