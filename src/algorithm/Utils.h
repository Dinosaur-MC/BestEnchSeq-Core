#pragma once
#include "types/CompactedTypes.h"

/// Check whether \p equipment satisfies all enchantments in \p target.
/// Returns true when every target enchantment is present at or above
/// the required level.
inline bool meets_target(
    const compact::Item& equipment,
    const compact::EnchCollection& target
) noexcept {
    for (const auto& t : target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}
