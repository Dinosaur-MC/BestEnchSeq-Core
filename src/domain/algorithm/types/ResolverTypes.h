#pragma once
#include "domain/algorithm/types/Item.h"
#include <cstdint>
#include <vector>

namespace algorithm {

/// Output of any resolver.  Empty vector means "unreachable / no work needed".
using ResolverOutput = ItemCollection;

/// Input for direct-mode resolution.
struct DirectResolverInput {
    Item target_item;
    EnchSet source_ench;
};

/// Input for inventory-mode resolution.
struct InventoryResolverInput {
    Item target_item;
    ItemCollection available_items;
    std::vector<int32_t> priorities;
};

} // namespace algorithm
