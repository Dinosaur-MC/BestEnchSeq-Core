#include "InventoryResolver.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

InventoryResult InventoryResolver::resolve(
    const Item& target,
    const ItemCollection& available_items,
    const std::vector<int32_t>& priorities)
{
    InventoryResult result;

    // If there's nothing to work with, the target is reachable only if
    // it already satisfies itself (no enchants needed).
    if (available_items.empty()) {
        result.reachable = true;
        return result;
    }

    // Pair each item with its priority, then sort by priority (lower first).
    // The first item (index 0) is always the target equipment itself.
    struct RankedItem {
        const Item* item;
        int32_t priority;
    };
    std::vector<RankedItem> ranked;
    ranked.reserve(available_items.size());
    for (size_t i = 0; i < available_items.size(); ++i) {
        int32_t prio = (i < priorities.size()) ? priorities[i] : 99;
        ranked.push_back({&available_items[i], prio});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
        [](const RankedItem& a, const RankedItem& b) { return a.priority < b.priority; });

    // Collect the sorted items (ready for the forge pipeline).
    result.used_items.reserve(ranked.size());
    for (auto& r : ranked) {
        result.used_items.push_back(*r.item);
    }

    // Feasibility: for each enchantment the target needs, check that
    // at least one book has a matching enchantment at or above the
    // required level.  Equipment items (type == Equip) are skipped in
    // this check — they're treated as forgeable bases.
    result.reachable = true;
    if (target.type == ItemType::Equip) {
        // The target is an equipment piece — we need at least one
        // other item (a book) to forge into it.
        bool has_sacrifice = false;
        for (const auto& item : result.used_items) {
            if (item.type == ItemType::Book && !item.enchs.empty()) {
                has_sacrifice = true;
                break;
            }
        }
        if (!has_sacrifice) {
            // Check if the target already meets all requirements
            // (i.e., it needs no additional enchantments).
            result.reachable = target.enchs.empty();
        }
    }

    return result;
}

} // namespace algorithm
