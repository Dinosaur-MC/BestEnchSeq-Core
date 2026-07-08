#include "DFSAlgorithm.h"
#include "utils/ExpCalculator.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>

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

    // P0: Greedy upper bound — compute a solution estimate to establish a tight
    // _best_cost before DFS starts, so branch-and-bound can prune from step one.
    greedy_upper_bound(items);

    // Launch recursive DFS
    dfs(items, 0, ctx);

    // Report the best solution found
    if (!_best_steps.empty()) {
        ctx.report_solution_found(_best_steps);
    }

    ctx.report_progress(1.0, "DFS search complete");
}

int32_t DFSAlgorithm::greedy_upper_bound(const std::vector<ItemStack>& items) {
    if (items.empty()) return 0;
    if (items.size() == 1) {
        if (meets_target(items[0], _input->target_item))
            _best_cost = 0;
        return 0;
    }

    // P1: Book-first merging strategy.
    // Phase 1: Merge all books together so penalty grows on the book stack
    // instead of on the equipment.  This keeps equipment penalty at 0 for
    // all but the final forge, producing a significantly tighter upper bound
    // than the linear approach (which would compound penalty on the equipment
    // with every step).
    ItemStack combined_book = items[1];
    int32_t total = 0;

    for (size_t k = 2; k < items.size(); ++k) {
        if (!_forge_engine.is_forgeable(combined_book, items[k]))
            continue;
        auto [result, cost] = _forge_engine.forge(combined_book, items[k]);
        total += cost;
        combined_book = result;
    }

    // Phase 2: Apply the combined book to the target equipment in one forge.
    ItemStack current = items[0];
    if (_forge_engine.is_forgeable(current, combined_book)) {
        auto [result, cost] = _forge_engine.forge(current, combined_book);
        total += cost;
        current = result;
    }

    // If the book-first greedy reached the goal, use its cost as the bound.
    if (meets_target(current, _input->target_item)) {
        _best_cost = total;
    }

    return total;
}

int32_t DFSAlgorithm::lower_bound(const EnchSet& current, const EnchSet& target) const {
    // Admissible heuristic: sum of (missing_level * book_multiplier) for each
    // target enchantment not already present at or above the required level.
    // Ignores conflicts, penalty costs, and cost cap — always <= true remaining cost.
    (void)_input; // kept for future platform-aware heuristics
    int32_t cost = 0;
    for (const Ench& e : target) {
        auto it = current.find(e);
        if (it == current.end()) {
            // Enchantment missing entirely
            cost += e.level * e.get_multiplier(true);
        } else if (it->level < e.level) {
            // Enchantment present but at a lower level — need to upgrade
            cost += (e.level - it->level) * e.get_multiplier(true);
        }
    }
    return cost;
}

bool DFSAlgorithm::meets_target(const ItemStack& item, const ItemStack& target) const {
    // Equipment type must match (pointer comparison — both reference the same singleton)
    if (item.equipment != target.equipment)
        return false;

    // All target enchantments must be present at or above the required level
    for (const Ench& e : target.enchantments) {
        auto it = item.enchantments.find(e);
        if (it == item.enchantments.end() || it->level < e.level)
            return false;
    }

    return true;
}

void DFSAlgorithm::dfs(std::vector<ItemStack>& items, int32_t cost_so_far, ExecutionContext& ctx) {
    // ── Check cancellation / pause ──
    if (ctx.is_cancelled())
        return;
    ctx.wait_if_paused();

    // ── State memoization — generate canonical signature from item multiset ──
    {
        std::string sig;
        for (const auto& item : items) {
            sig += "[" + std::to_string(item.prior_penalty) + ":";
            // Sort enchantments by id for canonical ordering
            std::vector<Ench> sorted(item.enchantments.begin(), item.enchantments.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const Ench& a, const Ench& b) { return a.id < b.id; });
            for (const auto& e : sorted)
                sig += std::to_string(e.id) + "," + std::to_string(e.level) + ";";
            sig += "]";
        }
        if (_visited.count(sig))
            return;
        _visited.insert(sig);
    }

    // ── Goal check (before pruning: a goal state must always be recorded) ──
    if (meets_target(items[0], _input->target_item)) {
        // Record the first solution found, or any strictly better solution.
        if (_best_steps.empty() || cost_so_far < _best_cost) {
            _best_cost  = cost_so_far;
            _best_steps = _current_steps;
        }
        return;
    }

    // ── Branch-and-bound pruning (only prune non-goal states) ──
    if (cost_so_far + lower_bound(items[0].enchantments, _input->target_item.enchantments)
        >= _best_cost) {
        return;
    }

    // ── P2: Collect all valid forge pairs, sorted by estimated cost ──
    // Ordering by cost means cheaper pairs (which are more likely to lead to
    // good solutions) are explored first.  This lets aggressive pruning from
    // a tight _best_cost kick in much earlier.
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

            // Quick cost estimate: penalty cost + sacrifice's enchantment value.
            // This is a cheap proxy; the actual forge cost includes the same terms.
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

        // Save state for backtracking
        ItemStack saved_i = items[i];
        ItemStack saved_j = items[j];

        // P0: Use forge_into — modifies items[i] in-place with zero EnchSet copies.
        // The old code called forge() (returns a new ItemStack) and then assigned
        // it to items[i], which copied the EnchSet twice.
        int32_t step_cost = _forge_engine.forge_into(items[i], items[j]);

        // Record step (use saved copies for the pre-forge state)
        _current_steps.push_back({
            saved_i,
            saved_j,
            step_cost,
            ExpCalculator::level_to_exp(step_cost)
        });

        // Remove sacrifice
        items.erase(items.begin() + j);

        // Adjust i index if the erase happened before i
        size_t adjusted_i = (j < i) ? i - 1 : i;

        // Recurse
        dfs(items, cost_so_far + step_cost, ctx);

        // Restore state: first restore the modified item, then re-insert the sacrifice.
        // This order is critical — inserting shifts indices >= j to the right.
        items[adjusted_i] = saved_i;
        items.insert(items.begin() + j, saved_j);
        _current_steps.pop_back();

        // Early exit if cancelled during recursion
        if (ctx.is_cancelled())
            return;
    }
}
