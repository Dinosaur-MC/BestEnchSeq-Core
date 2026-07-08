#include "AStarAlgorithm.h"
#include "utils/ExpCalculator.hpp"

#include <climits>
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Heuristic: admissible lower bound on remaining cost.
//
// Collects the maximum level of each enchantment across all items, then for
// each target enchantment that is not yet satisfied computes
//   (missing_level * book_multiplier).
//
// Ignores penalty costs, incompatibility, and the 39-level cost cap, so it
// always underestimates (or equals) the true remaining forge cost.
// ─────────────────────────────────────────────────────────────────────────────
int32_t AStarAlgorithm::heuristic(
    const std::vector<ItemStack>& items, const EnchSet& target) const
{
    // Build map: ench_id -> max level present across all items
    std::unordered_map<int32_t, int32_t> max_levels;
    for (const auto& item : items) {
        for (const auto& ench : item.enchantments) {
            auto it = max_levels.find(ench.id);
            if (it == max_levels.end() || ench.level > it->second)
                max_levels[ench.id] = ench.level;
        }
    }

    int32_t h = 0;
    for (const Ench& t : target) {
        auto it = max_levels.find(t.id);
        int32_t have = (it == max_levels.end()) ? 0 : it->second;
        if (have < t.level) {
            int32_t needed = t.level - have;
            h += needed * t.get_multiplier(true);  // book multiplier (<= equipment)
        }
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Goal check: equipment pointer matches AND all target enchantments are present
// at or above the required level.
// ─────────────────────────────────────────────────────────────────────────────
bool AStarAlgorithm::meets_target(const ItemStack& item, const ItemStack& target) const {
    if (item.equipment != target.equipment)
        return false;
    for (const Ench& e : target.enchantments) {
        auto it = item.enchantments.find(e);
        if (it == item.enchantments.end() || it->level < e.level)
            return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A* execute
//
// Uses a standard graph-search A* with an admissible heuristic:
//   - `open_set`   : min-heap of (state, f = g + h)
//   - `best_g`     : map state -> best-known g-cost (closed set with g-tracking)
//
// State identity is based on item MULTISET content (enchantments + prior_penalty
// for each item). items[0] is always the equipment; remaining indices are books.
//
// The first goal state popped from the open set is guaranteed optimal because
// the heuristic is admissible (never overestimates).
// ─────────────────────────────────────────────────────────────────────────────
void AStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, "starting A* search");

    _input = &input;

    // ── Initial state ─────────────────────────────────────────────────────
    // items[0] = equipment with original_ench, prior_penalty = 0
    ItemStack start_item(
        input.target_item.equipment,
        input.original_ench,
        0
    );

    std::vector<ItemStack> items;
    items.reserve(1 + input.available_items.size());
    items.push_back(start_item);
    items.insert(items.end(), input.available_items.begin(), input.available_items.end());

    // Quick check: goal already met?
    if (meets_target(items[0], input.target_item)) {
        ctx.report_progress(1.0, "goal already met");
        ctx.report_solution_found({});
        return;
    }

    // Initial heuristic
    int32_t h0 = heuristic(items, input.target_item.enchantments);

    // ── Open set: min-heap ordered by f = g + h ───────────────────────────
    std::priority_queue<PriorityState,
                        std::vector<PriorityState>,
                        std::greater<>> open_set;

    open_set.push({{std::move(items), 0, {}}, h0});

    // ── Closed set: state -> best known g-cost ────────────────────────────
    // StateEqual and StateHash compare by items content only (not g or steps).
    std::unordered_map<SearchState, int32_t, StateHash, StateEqual> best_g;

    int64_t explored = 0;

    // ── Main A* loop ─────────────────────────────────────────────────────
    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        // Pop the state with lowest f = g + h
        PriorityState current = std::move(
            const_cast<PriorityState&>(open_set.top()));
        open_set.pop();

        // Skip stale entries: a better path to this state was already expanded
        auto bg_it = best_g.find(current.state);
        if (bg_it != best_g.end() && bg_it->second < current.state.g)
            continue;

        explored++;

        // Periodic progress reporting
        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(
                progress,
                "A* explored " + std::to_string(explored) +
                " states, open set: " + std::to_string(open_set.size()));
        }

        // ── Goal check ───────────────────────────────────────────────────
        // items[0] is always the equipment (equipment can never be sacrificed
        // because is_forgeable(book, equipment) is false).
        if (meets_target(current.state.items[0], input.target_item)) {
            // First goal popped = optimal (admissible heuristic guarantee)
            ctx.report_solution_found(current.state.steps);
            ctx.report_progress(
                1.0,
                "A* complete: optimal cost " + std::to_string(current.state.g));
            return;
        }

        // ── Expand: try every forgeable pair (i, j) ──────────────────────
        const size_t n = current.state.items.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!_forge_engine.is_forgeable(
                        current.state.items[i], current.state.items[j]))
                    continue;

                // Build child items by copying parent and applying forge
                std::vector<ItemStack> child_items = current.state.items;

                auto [result, step_cost] =
                    _forge_engine.forge(child_items[i], child_items[j]);

                child_items[i] = result;
                child_items.erase(child_items.begin() + j);

                int32_t child_g = current.state.g + step_cost;

                // Build step list for the child
                EnchStepList child_steps = current.state.steps;
                child_steps.push_back({
                    current.state.items[i],  // item_a before forge
                    current.state.items[j],  // item_b (sacrifice)
                    step_cost,
                    ExpCalculator::level_to_exp(step_cost)
                });

                SearchState child_state{
                    std::move(child_items),
                    child_g,
                    std::move(child_steps)
                };

                // Skip if we already know a better or equal path to this state
                auto c_it = best_g.find(child_state);
                if (c_it != best_g.end() && c_it->second <= child_g)
                    continue;

                // Compute heuristic for the child and push
                int32_t child_h =
                    heuristic(child_state.items, input.target_item.enchantments);
                int32_t child_f = child_g + child_h;

                best_g[child_state] = child_g;
                open_set.push({std::move(child_state), child_f});
            }
        }
    }

    // ── Termination without solution ─────────────────────────────────────
    if (ctx.is_cancelled()) {
        ctx.report_progress(1.0, "A* search cancelled");
    } else {
        ctx.report_progress(1.0, "A* search complete: no solution found");
    }
}
