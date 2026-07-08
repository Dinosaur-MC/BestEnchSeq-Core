#include "DFSAlgorithm.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/ExpCalculator.hpp"

#include <algorithm>
#include <cstdint>

DFSAlgorithm::StateKey DFSAlgorithm::make_state_key(
    const std::vector<ItemStack>& items) const
{
    StateKey key;
    key.penalties.reserve(items.size());

    // Count total enchantments across all items for pre-allocation
    size_t total_enchs = 0;
    for (const auto& item : items)
        total_enchs += item.enchantments.size();

    key.ench_ids.reserve(total_enchs);
    key.ench_levels.reserve(total_enchs);

    for (const auto& item : items) {
        key.penalties.push_back(item.prior_penalty);
        for (const Ench& e : item.enchantments) {
            key.ench_ids.push_back(e.id);
            key.ench_levels.push_back(e.level);
        }
    }

    return key;
}

void DFSAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, "starting DFS search");

    _input       = &input;
    _best_cost   = INT32_MAX;
    _best_steps.clear();
    _current_steps.clear();
    _visited.clear();

    // Build initial items: items[0] starts as the target equipment with
    // original_ench (the enchantments already present before forging).
    // target_item.enchantments defines the GOAL state (what we want to achieve).
    ItemStack start_item(
        input.target_item.equipment,
        input.original_ench,
        0  // prior_penalty: starting item has not been forged yet
    );

    std::vector<ItemStack> items;
    items.reserve(1 + input.available_items.size());
    items.push_back(start_item);
    items.insert(items.end(), input.available_items.begin(), input.available_items.end());

    // Greedy upper bound — compute a solution estimate to establish a tight
    // _best_cost before DFS starts, so branch-and-bound can prune from step one.
    if (items.size() > 1) {
        auto bound = AlgorithmUtils::book_first_merge(
            items[0], input.available_items, _forge_engine, ctx);
        _best_cost = bound.total_cost;
    }

    // Launch recursive DFS
    dfs(items, 0, ctx);

    // Report the best solution found
    if (!_best_steps.empty()) {
        ctx.report_solution_found(_best_steps);
    }

    ctx.report_progress(1.0, "DFS search complete");
}

void DFSAlgorithm::dfs(std::vector<ItemStack>& items, int32_t cost_so_far, ExecutionContext& ctx) {
    // ── Check cancellation / pause ──
    if (ctx.is_cancelled())
        return;
    ctx.wait_if_paused();

    // ── State memoization — use hash-based key (no string allocation) ──
    StateKey key = make_state_key(items);
    if (_visited.count(key))
        return;
    _visited.insert(std::move(key));

    // ── Goal check (before pruning: a goal state must always be recorded) ──
    if (AlgorithmUtils::meets_target(items[0], _input->target_item)) {
        if (_best_steps.empty() || cost_so_far < _best_cost) {
            _best_cost  = cost_so_far;
            _best_steps = _current_steps;
        }
        return;
    }

    // ── Branch-and-bound pruning ──
    if (cost_so_far + AlgorithmUtils::admissible_heuristic(
            items[0].enchantments, _input->target_item.enchantments)
        >= _best_cost) {
        return;
    }

    // ── Collect all valid forge pairs, sorted by estimated cost ──
    const size_t n = items.size();
    struct ForgePair { size_t i, j; int32_t est_cost; };
    std::vector<ForgePair> pairs;
    pairs.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j)
                continue;

            if (!_forge_engine.is_forgeable(items[i], items[j]))
                continue;

            int32_t est = ItemStack::get_penalty_cost(items[i].prior_penalty)
                        + ItemStack::get_penalty_cost(items[j].prior_penalty);
            for (const Ench& e : items[j].enchantments) {
                est += e.level * e.get_multiplier(items[j].is_book());
            }
            pairs.push_back({i, j, est});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const ForgePair& a, const ForgePair& b) { return a.est_cost < b.est_cost; });

    // ── Try each pair in cost order ──
    for (const auto& p : pairs) {
        size_t i = p.i;
        size_t j = p.j;

        ItemStack saved_i = items[i];
        ItemStack saved_j = items[j];

        int32_t step_cost = _forge_engine.forge_into(items[i], items[j]);

        _current_steps.push_back({
            saved_i,
            saved_j,
            step_cost,
            ExpCalculator::level_to_exp(step_cost)
        });

        items.erase(items.begin() + j);

        size_t adjusted_i = (j < i) ? i - 1 : i;

        dfs(items, cost_so_far + step_cost, ctx);

        items[adjusted_i] = saved_i;
        items.insert(items.begin() + j, saved_j);
        _current_steps.pop_back();

        if (ctx.is_cancelled())
            return;
    }
}
