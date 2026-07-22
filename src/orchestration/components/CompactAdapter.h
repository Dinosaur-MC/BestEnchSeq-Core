#pragma once
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include <vector>

struct CompactAdapter {
    /// Convert domain-level resolved input to compact AlgorithmInput.
    /// Config is NOT handled here -- flows directly from CLI.
    static algorithm::AlgorithmInput apply(
        const algorithm::ResolvedInput& resolved,
        const EnchantmentRegistry& global_registry
    );

    static std::vector<Solution> recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& input,
        const EnchSet& original_ench,
        const Item& target_item,
        const ItemCollection& available_items
    );

    static Item from_domain(const Item& item, const EnchantmentRegistry& reg);
    static Item to_domain(const algorithm::Item& item, const algorithm::EnchReg& reg);
};
