#include "InventoryResolver.h"
#include <algorithm>

namespace algorithm {

ResolverOutput InventoryResolver::resolve(
    const Item& target,
    const ItemCollection& available_items,
    const std::vector<int32_t>& priorities)
{
    // Empty collection → unreachable
    if (available_items.empty())
        return {};

    // Pair items with priority, sort by priority (lower first)
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
        [](const RankedItem& a, const RankedItem& b) {
            return a.priority < b.priority;
        });

    // Collect sorted items
    ResolverOutput result;
    result.reserve(ranked.size());
    for (auto& r : ranked)
        result.push_back(*r.item);

    // Feasibility: for equipment targets, must have at least one book
    if (target.type == ItemType::Equip) {
        bool has_sacrifice = false;
        for (const auto& item : result) {
            if (item.type == ItemType::Book && !item.enchs.empty()) {
                has_sacrifice = true;
                break;
            }
        }
        if (!has_sacrifice) {
            return {};  // unreachable: no sacrifice item for equipment
        }
    }

    return result;
}

} // namespace algorithm
