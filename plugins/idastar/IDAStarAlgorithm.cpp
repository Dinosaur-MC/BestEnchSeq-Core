#include "IDAStarAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/components/StateHash.h"
#include <algorithm>

namespace algorithm {

using namespace algorithm;

bool IDAStarAlgorithm::_meets_target(const std::vector<ItemID>& ids) const {
    if (ids.empty()) return false;
    const auto& equip = _pool[ids[0]];
    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

void IDAStarAlgorithm::_dfs(std::vector<ItemID>& ids, int32_t g,
                             int32_t& best_cost, ExecutionContext& ctx)
{
    ctx.incr_nodes_visited();
    ++_nodes_visited;

    if (_nodes_visited % 512 == 0) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        if (_max_search_time.count() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - _start_time;
            if (elapsed > _max_search_time) return;
        }
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

    // Heuristic pruning (_h_max was precomputed by caller or root)
    int32_t h_val = _compute_h();
    if (g + h_val >= best_cost) {
        ctx.incr_nodes_pruned();
        return;
    }

    // TT: global best_g
    size_t h = StateHash::ids(ids, _pool);
    ++_diag.tt_lookups;
    if (const int32_t* tt_g = _tt.lookup(h)) {
        if (*tt_g <= g) {
            ctx.incr_nodes_pruned();
            return;
        }
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
            int32_t est = _forge_engine.estimate_forge_cost(
                _pool[ids[i]], _pool[ids[j]], *_ench_reg);
            if (g + est <= best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
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

        _current_path.push_back(IDALightStep{old_base_id, old_sac_id, real_cost});

        // Delta heuristic: update _h_max for child, recurse, restore
        {
            auto saved_h_max = _h_max;
            const auto& forged_enchs = _pool[new_base_id].enchs;
            for (const auto& e : forged_enchs) {
                if (e.level > _h_max[e.id])
                    _h_max[e.id] = e.level;
            }
            _dfs(child_buf, child_g, best_cost, ctx);
            _h_max = std::move(saved_h_max);
        }

        _current_path.pop_back();

        if (ctx.is_cancelled()) return;
    }
}

int32_t IDAStarAlgorithm::_compute_h() const {
    return search_utils::compute_h(_target, *_ench_reg, _h_max);
}

void IDAStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.f_config);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
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
    _start_time = std::chrono::steady_clock::now();

    // Cache config from AlgorithmInput
    _max_solutions = input.s_config.max_solutions;
    _max_search_time = input.s_config.max_search_time;

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
    if (items.size() > 1) {
        Item equip = items[0];
        int32_t greedy_cost = 0;
        for (size_t k = 1; k < items.size(); ++k) {
            if (_forge_engine.is_forgeable(equip, items[k]))
                greedy_cost += _forge_engine.forge_into(equip, items[k], reg);
        }
        if (greedy_cost > 0) best_cost = greedy_cost;

        // External warm-start bound skips internal dfs_bound (~25M Ir)
        if (input.initial_bound < best_cost) {
            best_cost = input.initial_bound;
        } else {
            int64_t node_limit = 50'000;
            int32_t dfs_cost = search_utils::dfs_bound(
                std::vector<Item>(items.begin(), items.end()),
                0, best_cost, node_limit,
                _forge_engine, *_ench_reg, _target,
                _h_buf, _h_dirty);
            if (dfs_cost < best_cost)
                best_cost = dfs_cost;
        }
    }

    // Warm-start bound (fallback: already handled for items.size() > 1)
    if (input.initial_bound < best_cost)
        best_cost = input.initial_bound;

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
            steps.push_back({_pool[s.base_id], _pool[s.sac_id], s.cost});

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

} // namespace algorithm
