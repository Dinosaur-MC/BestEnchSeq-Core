#include "GreedyAlgorithm.h"
#include "utils/ExpCalculator.h"

void GreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    // Greedy strategy — simplified placeholder that demonstrates the pattern:
    // 1. Start with target_item
    // 2. For each wanted enchantment, find the cheapest book/item
    // 3. Forge it, accumulate result
    // 4. Progress reporting + solution streaming

    // This is a minimal implementation — real greedy logic comes in later iteration
    ctx.report_progress(0.0, "starting greedy search");

    ItemStack current = input.target_item;
    EnchStepList steps;
    int32_t step_index = 0;

    // For each available item, try the cheapest forge
    for (const auto& sacrifice : input.available_items) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        if (!_forge_engine.is_forgeable(current, sacrifice))
            continue;

        auto [result, cost] = _forge_engine.forge(current, sacrifice);
        steps.push_back({current, sacrifice, cost, ExpCalculator::level_to_exp(cost)});
        current = result;

        ctx.report_progress(
            (step_index + 1.0) / input.available_items.size(),
            "applying sacrifice"
        );
        step_index++;
    }

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, "done");
}
