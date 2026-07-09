#include "CompactGreedyAlgorithm.h"
#include "utils/ExpCalculator.hpp"
#include <algorithm>

void CompactGreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    // 1. Initialize compact registry for target equipment
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(
        EnchantmentRegistry::get_instance(),
        *input.target_item.equipment);

    // 2. Convert input to compact representation
    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;  // items[0] = equipment, rest = books

    // 3. Sort books by estimated cost (cheapest first)
    std::vector<BookCost> ordered;
    ordered.reserve(items.size() - 1);
    for (size_t i = 1; i < items.size(); ++i) {
        int32_t est = compact::estimate_forge_cost(items[0], items[i], ench_reg);
        ordered.push_back({i, est});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const BookCost& a, const BookCost& b) { return a.est_cost < b.est_cost; });

    // 4. Forge in cost order
    EnchStepList steps;
    int32_t step_index = 0;

    for (const auto& bc : ordered) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& target = items[0];
        const auto& sacrifice = items[bc.index];

        if (!compact::CompactForgeEngine::is_forgeable(target, sacrifice))
            continue;

        // Record step BEFORE forge (capture domain representations)
        ItemStack before_target = compact::to_domain(target, ci.equipment);
        ItemStack before_sacrifice = compact::to_domain(sacrifice, ci.equipment);

        // Perform forge
        int32_t cost = _forge_engine.forge_into(target, sacrifice, ench_reg);

        steps.push_back({
            std::move(before_target),
            std::move(before_sacrifice),
            cost,
            ExpCalculator::level_to_exp(cost)
        });

        ctx.report_progress(
            (step_index + 1.0) / ordered.size(),
            ProgressStatus::ApplyingSacrifice
        );
        step_index++;
    }

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
