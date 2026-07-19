#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/EnchSolution.h"
#include "registries/EnchantmentRegistry.h"
#include "types/AlgorithmTypes.h"
#include "resolvers/ItemResolver.h"
#include <vector>

struct CompactAdapter {
    /// Convert domain-level resolved input to compact AlgorithmInput.
    /// Config is NOT handled here -- flows directly from CLI.
    static AlgorithmInput apply(
        const ResolvedInput& resolved,
        const EnchantmentRegistry& global_registry
    );

    static std::vector<EnchSolution> recall(
        const AlgorithmOutput& output,
        const AlgorithmInput& input,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items
    );

    static compact::Item from_domain(const ItemStack& item, const compact::EnchReg& reg);
    static ItemStack to_domain(const compact::Item& item, const compact::EnchReg& reg);
};
