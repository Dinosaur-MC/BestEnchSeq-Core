#pragma once
#include "BESQTypes.h"

// ─── Algorithm input (domain boundary type) ───
// Produced by InputParser, consumed by CompactAdapter::prepare()
// to build compact items for the compact-only algorithm layer.
struct AlgorithmInput {
    platform::MCE platform;
    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
};
