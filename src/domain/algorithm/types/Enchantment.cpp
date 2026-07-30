#include "Enchantment.h"

namespace algorithm {

bool EnchInfo::is_conflict(const EnchInfo &other) const noexcept {
    return (1ULL << id) & other.exc_mask;
}

} // namespace algorithm
