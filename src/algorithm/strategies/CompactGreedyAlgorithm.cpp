#include "CompactGreedyAlgorithm.h"
#include "../ExecutionContext.h"
#include "utils/CompactForgeUtils.hpp"
#include <algorithm>

void CompactGreedyAlgorithm::execute(
    const std::vector<compact::Item>& items,
    const compact::EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    // Sort books by estimated forge cost
    std::vector<BookCost> ordered;
    ordered.reserve(items.size() - 1);
    for (size_t i = 1; i < items.size(); ++i) {
        int32_t est = compact::estimate_forge_cost(items[0], items[i], reg);
        ordered.push_back({i, est});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const BookCost& a, const BookCost& b) { return a.est_cost < b.est_cost; });

    // Forge in cost order
    std::vector<compact::EnchStep> compact_steps;
    int32_t step_index = 0;

    std::vector<compact::Item> mutable_items = items;

    for (const auto& bc : ordered) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& target_item = mutable_items[0];
        const auto& sacrifice = mutable_items[bc.index];

        if (!compact::CompactForgeEngine::is_forgeable(target_item, sacrifice))
            continue;

        compact::Item before_target = target_item;
        compact::Item before_sacrifice = sacrifice;

        int32_t cost = _forge_engine.forge_into(target_item, sacrifice, reg);

        compact_steps.push_back({
            std::move(before_target),
            std::move(before_sacrifice),
            cost
        });

        ctx.report_progress(
            (step_index + 1.0) / ordered.size(),
            ProgressStatus::ApplyingSacrifice
        );
        step_index++;
    }

    ctx.report_compact_solution(compact_steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
