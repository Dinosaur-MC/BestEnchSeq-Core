#include "CompactGreedyAlgorithm.h"
#include "utils/CompactAdapter.hpp"
#include "utils/ExpCalculator.hpp"
#include <algorithm>

void CompactGreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    //── Boundary: prepare compact data ────────────────────────────────────
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(
        EnchantmentRegistry::get_instance(),
        *input.target_item.equipment);

    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;

    // Sort books by estimated cost (compact-only)
    std::vector<BookCost> ordered;
    ordered.reserve(items.size() - 1);
    for (size_t i = 1; i < items.size(); ++i) {
        int32_t est = compact::estimate_forge_cost(items[0], items[i], ench_reg);
        ordered.push_back({i, est});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const BookCost& a, const BookCost& b) { return a.est_cost < b.est_cost; });

    // Forge in cost order — compact-only, no domain types
    std::vector<compact::EnchStep> compact_steps;
    int32_t step_index = 0;

    for (const auto& bc : ordered) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& target = items[0];
        const auto& sacrifice = items[bc.index];

        if (!compact::CompactForgeEngine::is_forgeable(target, sacrifice))
            continue;

        compact::Item before_target = target;
        compact::Item before_sacrifice = sacrifice;

        int32_t cost = _forge_engine.forge_into(target, sacrifice, ench_reg);

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

    //── Boundary: convert compact steps to domain for output ──────────────
    auto steps = compact::to_domain(
        compact_steps.begin(), compact_steps.end(), ci.equipment);

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
