#include "IDAStarAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <cmath>

namespace algorithm {

using namespace algorithm;

bool IDAStarAlgorithm::_meets_target(const std::vector<ItemID>& ids) const {
    if (ids.empty()) return false;
    const auto& equip = _pool[ids[0]];
    for (const auto& t : _target) {
        if (equip.enchs[t.id] < t.level)
            return false;
    }
    return true;
}

void IDAStarAlgorithm::_dfs(std::vector<ItemID>& ids, int32_t g,
                             int32_t& best_cost, ExecutionContext& ctx)
{
    ctx.incr_nodes_visited();
    ++_nodes_visited;

    if (ctx.is_cancelled()) return;

    if (_nodes_visited % 256 == 0) {
        ctx.wait_if_paused();

        // Guard: executor's timeout watcher may have cancelled ctx.
        if (ctx.is_cancelled()) return;

        // Hard node budget — prevents indefinite hangs on large problems
        // where the heuristic doesn't prune effectively.
        // NOTE: just returns without cancelling ctx — if a solution was
        // already found it will still be reported by execute() below.
        if (_nodes_visited > 40'000'000) return;
    }

    // Goal check BEFORE pruning — never prune a solution
    if (_meets_target(ids)) {
        ++_solutions_found;
        if (g <= best_cost) {
            best_cost = g;
            _solution_path = _current_path;
        }
        {
            if (_max_solutions > 0 && _solutions_found >= _max_solutions)
                ctx.cancel();
        }
        return;
    }

    // TT: global best_g — check BEFORE heuristic so that states already
    // explored with an equal-or-better g skip the heuristic computation.
    size_t h = _pool.hash_ids(ids);
    ++_diag.tt_lookups;
    if (const int32_t* tt_g = _tt.lookup(h)) {
        if (*tt_g <= g) {
            ctx.incr_nodes_pruned();
            return;
        }
    }

    // Heuristic pruning (_h_max was precomputed by caller or root)
    int32_t h_val = _compute_h();
    if (g + h_val >= best_cost) {
        ctx.incr_nodes_pruned();
        return;
    }

    _tt.store(h, g);
    ++_diag.tt_stores;

    // Collect forgeable pairs sorted by estimated cost
    struct Candidate { size_t i, j; int32_t est_cost; };
    const size_t n = ids.size();
    std::vector<Candidate> candidates;
    candidates.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!_forge_engine.is_forgeable(_pool[ids[i]], _pool[ids[j]]))
                continue;
            // Admissible lower bound: the standard estimate over-charges
            // sacrifice enchants forge_into will DROP on conflict, which
            // pruned the ONLY valid child → false "unreachable".
            int32_t est = admissible_forge_cost(
                _forge_engine, _pool[ids[i]], _pool[ids[j]], *_ench_reg);
            if (g + est <= best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  // Cost-first ordering: explore cheapest pairs first.
                  // Equipment-first converges fast but may miss solutions
                  // that require intermediate book-book merges.
                  return a.est_cost < b.est_cost;
              });

    // Reusable child buffer — allocated once per stack frame.
    // clear() retains capacity, push_back reuses the storage across all
    // candidate iterations, eliminating ~70M heap (malloc+free) pairs.
    std::vector<ItemID> child_buf;
    child_buf.reserve(n > 0 ? n - 1 : 0);

    for (const auto& cand : candidates) {
        if (g + cand.est_cost > best_cost) continue;

        ItemID old_base_id = ids[cand.i];
        ItemID old_sac_id  = ids[cand.j];
        Item forged = _pool[old_base_id];
        int32_t real_cost = _forge_engine.forge_into(forged, _pool[old_sac_id], *_ench_reg);
        int32_t child_g = g + real_cost;
        ctx.incr_steps_forged();

        if (child_g > best_cost) continue;

        // Build child state: selective copy — avoids the copy+erase pattern
        // that allocated a new heap buffer per candidate.
        child_buf.clear();
        for (size_t k = 0; k < cand.j; ++k)
            child_buf.push_back(ids[k]);
        for (size_t k = cand.j + 1; k < n; ++k)
            child_buf.push_back(ids[k]);
        size_t base_in_child = (cand.i > cand.j) ? cand.i - 1 : cand.i;

        ItemID new_base_id = _pool.add(std::move(forged));
        if (new_base_id == ItemPool::INVALID_ITEM_ID) continue;
        child_buf[base_in_child] = new_base_id;

        // Symmetry breaking: canonicalize non-equipment ordering.
        if (child_buf.size() > 2)
            std::sort(child_buf.begin() + 1, child_buf.end());

        _current_path.push_back(IDALightStep{old_base_id, old_sac_id, new_base_id, real_cost});

        // Delta heuristic: update _h_max for child, recurse, restore.
        // Stack-allocated delta array avoids the heap allocation + memcpy of
        // a full vector copy (~10M× for large searches).
        {
            struct { int16_t id; int16_t old_level; } deltas[64];
            size_t nd = 0;
            const auto& forged_enchs = _pool[new_base_id].enchs;
            for (const auto& e : forged_enchs) {
                if (e.level() > _h_max[e.id()]) {
                    deltas[nd++] = {e.id(), _h_max[e.id()]};
                    _h_max[e.id()] = e.level();
                }
            }
            _dfs(child_buf, child_g, best_cost, ctx);
            for (size_t d = 0; d < nd; ++d)
                _h_max[deltas[d].id] = deltas[d].old_level;
        }

        _current_path.pop_back();

        if (ctx.is_cancelled()) return;
    }
}

int32_t IDAStarAlgorithm::_compute_h() const {
    return search_utils::compute_h(_target, *_ench_reg, _h_max);
}

void IDAStarAlgorithm::execute(const AlgorithmInput &input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config.forge);
    auto items = get_resolver()->resolve(input);
    normalize_base_equipment(items);
    const auto& reg = input.registry;
    const auto& target = input.target;
    ctx.report_progress(0, ProgressStatus::Starting);

    _pool.clear();
    // Pre-allocate pool capacity based on problem size (factorial estimate).
    // Avoids ~O(log n) reallocations of the item vector + dedup hash table
    // during search.  At most 500k — well below the 10M default max.
    {
        size_t est = 64;
        if (items.size() > 1) {
            size_t f = 1;
            for (size_t k = 2; k <= items.size() && f <= (1u << 20); ++k) f *= k;
            est = std::max<size_t>(64, f);
        }
        _pool.reserve(std::min(est, size_t{500'000}));
    }
    _tt.clear();
    _current_path.clear();
    _solution_path.clear();
    _ench_reg = &reg;
    _target.clear();
    for (const auto& e : target.enchs)
        _target.push_back(e);
    _target_level_map.assign(_ench_reg->size(), 0);
    for (const auto& t : _target)
        _target_level_map[t.id] = t.level;
    _nodes_visited = 0;
    _solutions_found = 0;

    // Cache config from AlgorithmInput
    _max_solutions = input.config.search.max_solutions;

    std::vector<ItemID> initial_ids;
    initial_ids.reserve(items.size());
    for (const auto& item : items)
        initial_ids.push_back(_pool.add(Item(item)));

    if (initial_ids.empty() || (initial_ids.size() == 1 && !_meets_target(initial_ids))) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }
    if (_meets_target(initial_ids)) {
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        ctx.report_solution({});
        return;
    }

    // Upper bound: greedy + limited DFS
    int32_t best_cost = INT32_MAX;
    bool has_realistic_bound = false;
    if (items.size() > 1) {
        Item equip = items[0];
        int32_t greedy_cost = 0;
        for (size_t k = 1; k < items.size(); ++k) {
            if (_forge_engine.is_forgeable(equip, items[k]))
                greedy_cost += _forge_engine.forge_into(equip, items[k], reg);
        }
        // Only use greedy cost as a bound if it produced a VALID solution.
        // When enchantment conflicts exist (common with modded profiles),
        // forge_into silently skips conflicting enchants, making the greedy
        // cost unrealistic — using it as a bound would prune paths that
        // spend extra levels to resolve conflicts, preventing the DFS from
        // ever finding a valid solution.
        if (greedy_cost > 0 && meets_target(equip, input.target)) {
            best_cost = greedy_cost;
            has_realistic_bound = true;
        }

        // External warm-start bound
        if (input.config.search.initial_bound < best_cost) {
            best_cost = input.config.search.initial_bound;
            has_realistic_bound = true;
        } else {
            int64_t node_limit = 50'000;
            int32_t dfs_cost = search_utils::dfs_bound(
                std::vector<Item>(items.begin(), items.end()),
                0, best_cost, node_limit,
                _forge_engine, *_ench_reg, input.target,
                _h_buf, _h_dirty);
            if (dfs_cost < best_cost) {
                best_cost = dfs_cost;
                has_realistic_bound = true;
            }
        }
    }

    // If neither greedy nor dfs_bound found a valid bound (should be rare),
    // fall back to a loose bound estimate: minimum possible forge count ×
    // the smallest multiplier, plus level-based heuristic.  Still admissible
    // and prevents the unbounded DFS that would make large problems hang.
    if (!has_realistic_bound && items.size() > 1) {
        // Conservative heuristic base: each forge costs at least the minimum
        // enchantment cost (penalty_cost for ppn=0 + ppn=0 = 0, plus at least
        // 1 level per sacrifice enchantment × min multiplier).
        int32_t min_cost_per_forge = 1;
        best_cost = static_cast<int32_t>(items.size() - 1) * min_cost_per_forge;
        // Add the greedy cost (even if it didn't meet target, it's a data point)
        for (size_t k = 1; k < items.size(); ++k) {
            Item equip_tmp = items[0];
            if (_forge_engine.is_forgeable(equip_tmp, items[k]))
                best_cost += _forge_engine.forge_into(equip_tmp, items[k], reg);
        }
        has_realistic_bound = true;
    }

    // Exhaustive DFS branch-and-bound
    search_utils::precompute_max(initial_ids, _pool, *_ench_reg,
                                  _h_max, _h_dirty);
    _dfs(initial_ids, 0, best_cost, ctx);

    _diag.items_pool_used = _pool.size();
    _diag.items_pool_capacity = _pool.capacity();
    _diag.solutions_found = _solutions_found;
    _diag.final_bound = best_cost;

    if (best_cost < INT32_MAX && !_solution_path.empty()) {
        _diag.solution_path_len = _solution_path.size();
        _diag.solution_cost = best_cost;
        _diag.status = "Complete";

        std::vector<EnchStep> steps;
        steps.reserve(_solution_path.size());
        for (const auto& s : _solution_path)
            steps.push_back({_pool[s.base_id], _pool[s.sac_id], _pool[s.result_id], s.cost});

        ctx.report_solution(steps);
        ctx.report_progress(100, ProgressStatus::Complete);
    } else {
        _diag.status = ctx.is_cancelled() ? "Cancelled" : "CompleteNoSolution";
        ctx.report_progress(100,
            ctx.is_cancelled() ? ProgressStatus::Cancelled
                               : ProgressStatus::CompleteNoSolution);
    }
    ctx.set_exit_diagnostics(_diag);
}


// ─── evaluate ──────────────────────────────────────────────────────────────────

double IDAStarAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // Fitted from best_benchmark.txt (Release, 2026-08-06, sword_9 9 / sword_12
    // 12 enchs tail):
    //   t(e) ≈ 7.04e-5 × 3.496^e   seconds   (2-point fit +30% safety)
    // Feasible ≤ 9 at the 10 s tier-4 budget; 10+ measured no solution.
    return 0.0310 * std::exp(1.4653e-4 * std::pow(3.0, static_cast<double>(ench_count)));
}


} // namespace algorithm
