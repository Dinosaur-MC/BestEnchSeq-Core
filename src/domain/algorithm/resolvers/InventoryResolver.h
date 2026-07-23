#pragma once
#include "domain/algorithm/types/ResolverTypes.h"

namespace algorithm {

/// Inventory-mode feasibility analyzer.
///
/// Sorts available items by priority and checks reachability.
/// Returns empty vector if unreachable.
struct InventoryResolver {
    static ResolverOutput resolve(const Item& target,
                                   const ItemCollection& available_items,
                                   const std::vector<int32_t>& priorities);
};

} // namespace algorithm
