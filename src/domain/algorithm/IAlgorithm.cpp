#include "IAlgorithm.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/InventoryResolver.h"
#include "domain/algorithm/resolvers/ItemResolver.h"

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

bool IAlgorithm::simulate(const AlgorithmInput &input) const noexcept {
    if (input.items.empty())
        return false;
    if (meets_target(input.items[0], input.target.enchs))
        return true;
    return input.items.size() > 1;
}

std::optional<Item> IAlgorithm::process(const EnchSolution &solution, const ForgeConfig &cfg, const EnchReg &reg) const {
    if (solution.steps.empty())
        return std::nullopt;

    auto engine = get_forge_engine();
    if (!engine)
        return std::nullopt;
    engine->set_config(cfg);

    Item result = solution.steps[0].base;
    for (const auto &step : solution.steps) {
        if (step.base.type == ItemType::Equip)
            result = step.base;
        if (!engine->is_forgeable(result, step.sacrifice))
            return std::nullopt;
        engine->forge_into(result, step.sacrifice, reg);
    }
    return result;
}

} // namespace algorithm
