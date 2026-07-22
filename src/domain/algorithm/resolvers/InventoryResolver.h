#pragma once
#include "../types/Item.h"
#include "../types/Solution.h"
#include "../forge_engine/IForgeEngine.h"
#include <vector>

namespace algorithm {

/// Input to inventory-mode resolution: desired target and available items.
struct InventoryInput {
    Item target;                    ///< Desired final item (equipment + enchantments)
    ItemCollection available_items; ///< Books and equipment to work with
    std::vector<int32_t> priorities;///< Priority per available item (lower = preferred)
};

/// Result of inventory resolution.
struct InventoryResult {
    bool reachable = false;     ///< Whether target is reachable from available items
    ItemCollection used_items;  ///< Recommended subset to use (priority-sorted)
};

/// Inventory-mode feasibility analyzer.
///
/// Given a target item and a pool of available items with priorities,
/// determines whether the target is reachable via forge operations and
/// returns the recommended subset of items to use.  Operates purely on
/// algorithm domain types — all ID resolution and JSON parsing must happen
/// before calling this.
struct InventoryResolver {
    static InventoryResult resolve(
        const Item& target,
        const ItemCollection& available_items,
        const std::vector<int32_t>& priorities
    );
};

} // namespace algorithm
