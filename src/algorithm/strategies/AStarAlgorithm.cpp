#include "AStarAlgorithm.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/ExpCalculator.hpp"

#include <queue>
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
    return AlgorithmUtils::meets_target(item, target);
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
// Optimization: steps are stored as a linked list (StepNode) rather than a
// flat vector, so copying from parent to child is O(1) instead of O(depth).
// The full list is flattened only when a solution is found.
//
// The first goal state popped from the open set is guaranteed optimal because
// the heuristic is admissible (never overestimates).
// ─────────────────────────────────────────────────────────────────────────────
void AStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    _input = &input;
    _step_pool.clear();

    // ── Initial state ─────────────────────────────────────────────────────
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
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_solution_found({});
        return;
    }

    // Initial heuristic
    int32_t h0 = heuristic(items, input.target_item.enchantments);

    // ── Open set: min-heap ordered by f = g + h ───────────────────────────
    std::priority_queue<PriorityState,
                        std::vector<PriorityState>,
                        std::greater<>> open_set;

    SearchState init_state{std::move(items), 0, nullptr};
    open_set.push({std::move(init_state), h0});

    // ── Closed set: state -> best known g-cost ────────────────────────────
    std::unordered_map<SearchState, int32_t, StateHash, StateEqual> best_g;

    int64_t explored = 0;

    // ── Main A* loop ─────────────────────────────────────────────────────
    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        PriorityState current = std::move(
            const_cast<PriorityState&>(open_set.top()));
        open_set.pop();

        // Skip stale entries
        auto bg_it = best_g.find(current.state);
        if (bg_it != best_g.end() && bg_it->second < current.state.g)
            continue;

        explored++;

        // Periodic progress reporting
        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        // ── Goal check ───────────────────────────────────────────────────
        if (meets_target(current.state.items[0], input.target_item)) {
            // First goal popped = optimal (admissible heuristic guarantee)
            ctx.report_solution_found(current.state.flatten_steps());
            ctx.report_progress(1.0, ProgressStatus::Complete);
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

                // Build child items by copying parent
                std::vector<ItemStack> child_items = current.state.items;

                auto [result, step_cost] =
                    _forge_engine.forge(child_items[i], child_items[j]);

                child_items[i] = result;
                child_items.erase(child_items.begin() + j);

                int32_t child_g = current.state.g + step_cost;

                // O(1): allocate step node linked to parent's tail
                const StepNode* step_node = alloc_step(
                    current.state.steps_tail,
                    EnchSolution::EnchStep{
                        current.state.items[i],
                        current.state.items[j],
                        step_cost,
                        ExpCalculator::level_to_exp(step_cost)
                    });

                SearchState child_state{
                    std::move(child_items),
                    child_g,
                    step_node
                };

                // Skip if we already know a better or equal path to this state
                auto c_it = best_g.find(child_state);
                if (c_it != best_g.end() && c_it->second <= child_g)
                    continue;

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
        ctx.report_progress(1.0, ProgressStatus::Cancelled);
    } else {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
    }
}
