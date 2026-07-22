#pragma once
#include "algorithm-domain/types/Item.h"
#include "algorithm-domain/types/Solution.h"
#include "algorithm-domain/types/AlgorithmTypes.h"
#include "business-domain/registries/EnchantmentRegistry.h"
#include "business-domain/registries/EnchantmentRegistry.h"
#include "business-domain/types/Item.h"
#include "business-domain/types/Solution.h"
#include "algorithm-domain/components/ItemResolver.h"
#include <vector>

struct CompactAdapter {
    /// Convert domain-level resolved input to compact AlgorithmInput.
    /// Config is NOT handled here -- flows directly from CLI.
    static algorithm::AlgorithmInput apply(
        const algorithm::ResolvedInput& resolved,
        const algorithm::EnchReg& global_registry
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
