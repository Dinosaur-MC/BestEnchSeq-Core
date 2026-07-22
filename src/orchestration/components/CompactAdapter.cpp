#include "CompactAdapter.h"
#include "common/utils/ExpCalculator.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

algorithm::AlgorithmInput CompactAdapter::apply(
    const algorithm::ResolvedInput& resolved,
    const EnchantmentRegistry& global_registry)
{
    return {};
}

Item CompactAdapter::from_domain(const Item& item, const EnchantmentRegistry& reg) {
    return {};
}

Item CompactAdapter::to_domain(const algorithm::Item& item, const algorithm::EnchReg& reg) {
    return {};
}

std::vector<Solution> CompactAdapter::recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& input,
    const EnchSet& original_ench,
    const Item& target_item,
    const ItemCollection& available_items)
{
    return {};
}
