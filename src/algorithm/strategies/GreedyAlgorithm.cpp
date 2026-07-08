#include "GreedyAlgorithm.h"
#include "utils/ExpCalculator.hpp"
#include <algorithm>
#include <vector>

void GreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    // Greedy strategy: forge each available book sequentially onto the
    // equipment.  At each step, pick the forgeable sacrifice with the
    // lowest estimated cost (cheapest first), which minimizes the penalty
    // that more expensive later steps pay.
    //
    // This sequential approach is optimal for small item counts and provides
    // a reasonable baseline for larger ones.  Book-first merging (merging all
    // books together then applying once) is intentionally NOT used here — it
    // produces worse results because the combined book's enchantments all
    // pay the equipment multiplier at once instead of incrementally.

    ItemStack current = input.target_item;
    EnchStepList steps;
    int32_t step_index = 0;

    // Build a list of (index, estimated_cost) pairs to sort by cost
    // So we apply cheaper sacrifices first (lower penalty accumulation)
    struct BookCost {
        size_t index;
        int32_t est_cost;
    };
    std::vector<BookCost> ordered;
    ordered.reserve(input.available_items.size());
    for (size_t i = 0; i < input.available_items.size(); ++i) {
        int32_t est = 0;
        for (const Ench& e : input.available_items[i].enchantments) {
            est += e.level * e.get_multiplier(true);  // book mult
        }
        ordered.push_back({i, est});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const BookCost& a, const BookCost& b) { return a.est_cost < b.est_cost; });

    // Forge in cost order
    for (const auto& bc : ordered) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        const auto& sacrifice = input.available_items[bc.index];

        if (!_forge_engine.is_forgeable(current, sacrifice))
            continue;

        auto [result, cost] = _forge_engine.forge(current, sacrifice);
        steps.push_back({
            current,
            sacrifice,
            cost,
            ExpCalculator::level_to_exp(cost)
        });
        current = result;

        ctx.report_progress(
            (step_index + 1.0) / input.available_items.size(),
            ProgressStatus::ApplyingSacrifice
        );
        step_index++;
    }

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
