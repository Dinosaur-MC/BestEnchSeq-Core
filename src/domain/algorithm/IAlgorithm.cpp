#include "IAlgorithm.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include "domain/algorithm/resolvers/InventoryResolver.h"

namespace algorithm {

ResolverOutput IAlgorithm::resolve(const AlgorithmInput &input) const {
    if (input.items.empty())
        return {};

    switch (input.mode) {
    case AlgorithmMode::direct: {
        // items[0] carries source enchants; target holds desired.
        // Build a temporary Item with desired enchantments for the resolver.
        Item desired = input.items[0];
        desired.enchs.clear();
        for (const auto &e : input.target.enchs)
            desired.enchs.insert(e);
        return ItemResolver::resolve(desired, input.items[0].enchs);
    }
    case AlgorithmMode::inventory: {
        // items[0] = equipment; items[1..] = available pool.
        ItemCollection extra;
        if (input.items.size() > 1) {
            extra.reserve(input.items.size() - 1);
            for (size_t i = 1; i < input.items.size(); ++i)
                extra.push_back(input.items[i]);
        }
        return InventoryResolver::resolve(input.items[0], extra, input.priorities);
    }
    }

    return {};
}

} // namespace algorithm
