#include "algorithm/strategies/dfs/DFSAlgorithm.h"
#include "algorithm/ExecutionContext.h"
#include "algorithm/components/SearchUtils.h"
#include "algorithm/components/HeuristicBasic.h"
#include <algorithm>
#include <cstdint>
#include <chrono>

#include <vector>

using namespace compact;

// ─── Compact-only greedy bound ─────────────────────────────────────────────

int32_t DFSAlgorithm::_greedy_bound(
    const std::vector<Item>& items,
    const EnchReg& reg) const
{
    if (items.size() <= 1) return 0;

    Item equip = items[0];
    std::vector<Item> books;
    books.reserve(items.size() - 1);
    for (size_t k = 1; k < items.size(); ++k)
        books.push_back(items[k]);

    int32_t total_cost = 0;

    std::vector<std::pair<size_t, int32_t>> ordered;
    for (size_t i = 0; i < books.size(); ++i)
        ordered.emplace_back(i, _forge_engine.estimate_forge_cost(equip, books[i], reg));
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& [idx, _] : ordered) {
        if (!_forge_engine.is_forgeable(equip, books[idx]))
            continue;
        int32_t cost = _forge_engine.forge_into(equip, books[idx], reg);
        total_cost += cost;
    }

    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return INT32_MAX;
    }
    return total_cost;
}

// ─── Collect forge pairs ───────────────────────────────────────────────────

std::vector<DFSAlgorithm::ForgePair> DFSAlgorithm::_collect_pairs(
    const std::vector<Item>& items) const
{
    const size_t n = items.size();
    std::vector<ForgePair> pairs;
    pairs.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!_forge_engine.is_forgeable(items[i], items[j]))
                continue;

            int32_t est = _forge_engine.estimate_forge_cost(items[i], items[j], *_ench_reg);
            pairs.push_back({i, j, est});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const ForgePair& a, const ForgePair& b) { return a.est_cost < b.est_cost; });
    return pairs;
}

// ─── Hot-path helpers ──────────────────────────────────────────────────────

int32_t DFSAlgorithm::_heuristic(const std::vector<Item>& items) const {
    return HeuristicBasic::compute(items, *_ench_reg, _target,
        _h_buf, _h_dirty);
}

// ─── execute ───────────────────────────────────────────────────────────────

void DFSAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    ctx.report_progress(0.0, ProgressStatus::Starting);
    _start_time = std::chrono::steady_clock::now();

    _ench_reg = &input.ench_reg;
    _search_config = input.search;
    _target = target;

    _best_cost = INT32_MAX;
    _best_steps.clear();
    _current_steps.clear();
    _visited_best.clear();
    _stack.clear();
    _frame_pairs.clear();
    _solutions_found = 0;

    if (items.size() > 1)
        _best_cost = _greedy_bound(items, reg);

    // Warm-start bound from executor chain (tighter than our own)
    if (input.initial_bound < _best_cost)
        _best_cost = input.initial_bound;

    _stack.push_back({items, 0, 0, 0, {}, {}, 0, 0, false});
    _frame_pairs.emplace_back();

    _dfs_iterative(ctx);

    _diag.solution_cost = _best_cost < INT32_MAX ? _best_cost : -1;
    _diag.final_bound = _best_cost;
    _diag.solutions_found = _solutions_found;
    _diag.status = _best_cost < INT32_MAX ? "Complete" : "CompleteNoSolution";
    ctx.set_exit_diagnostics(std::make_unique<SearchDiagnostics>(std::move(_diag)));

    ctx.report_progress(1.0, _best_cost < INT32_MAX
        ? ProgressStatus::Complete
        : ProgressStatus::CompleteNoSolution);
}

// ─── Iterative search ──────────────────────────────────────────────────────

void DFSAlgorithm::_dfs_iterative(ExecutionContext& ctx) {
    while (!_stack.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();
        ctx.incr_nodes_visited();

        // Use index instead of reference — _stack.push_back() in the loop body
        // may reallocate the vector and invalidate all references/iterators.
        size_t frame_idx = _stack.size() - 1;

        if (_stack[frame_idx].has_backtrack) {
            size_t adj_base = (_stack[frame_idx].sac_idx < _stack[frame_idx].base_idx)
                ? _stack[frame_idx].base_idx - 1 : _stack[frame_idx].base_idx;
            _stack[frame_idx].items[adj_base] = std::move(_stack[frame_idx].saved_base);
            _stack[frame_idx].items.insert(
                _stack[frame_idx].items.begin() + _stack[frame_idx].sac_idx, std::move(_stack[frame_idx].saved_sac));
            _current_steps.resize(_stack[frame_idx].saved_steps_size);
            _stack[frame_idx].has_backtrack = false;
        }

        {
            auto it = _visited_best.find(_stack[frame_idx].items);
            if (it != _visited_best.end() && it->second <= _stack[frame_idx].cost_so_far) {
                ctx.incr_nodes_pruned();
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
            _visited_best[_stack[frame_idx].items] = _stack[frame_idx].cost_so_far;
        }

        if (meets_target(_stack[frame_idx].items[0], _target)) {
            ++_solutions_found;
            ctx.report_solution(std::move(_current_steps));

            if (_best_steps.empty() || _stack[frame_idx].cost_so_far < _best_cost) {
                _best_cost = _stack[frame_idx].cost_so_far;
                _best_steps = _current_steps;
            }
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        if (_stack[frame_idx].cost_so_far + _heuristic(_stack[frame_idx].items) >= _best_cost) {
            ctx.incr_nodes_pruned();
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        {
            const auto& cfg = _search_config;
            if (cfg.max_depth > 0 &&
                static_cast<int32_t>(_stack.size()) > cfg.max_depth) {
                ctx.incr_nodes_pruned();
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
            if (cfg.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - _start_time;
                if (elapsed > cfg.max_search_time) break;
            }
            if (cfg.max_solutions > 0 && _solutions_found >= cfg.max_solutions)
                break;
        }

        auto& pairs = _frame_pairs.back();
        if (pairs.empty())
            pairs = _collect_pairs(_stack[frame_idx].items);

        if (_stack[frame_idx].pair_index >= pairs.size()) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        const auto& p = pairs[_stack[frame_idx].pair_index++];

        _stack[frame_idx].saved_base = _stack[frame_idx].items[p.i];
        _stack[frame_idx].saved_sac = _stack[frame_idx].items[p.j];
        _stack[frame_idx].base_idx = p.i;
        _stack[frame_idx].sac_idx = p.j;

        int32_t step_cost = _forge_engine.forge_into(
            _stack[frame_idx].items[p.i], _stack[frame_idx].items[p.j], *_ench_reg);
        ctx.incr_steps_forged();

        _current_steps.push_back({
            _stack[frame_idx].saved_base, _stack[frame_idx].saved_sac, step_cost
        });

        _stack[frame_idx].items.erase(_stack[frame_idx].items.begin() + p.j);

        std::vector<Item> child_items = _stack[frame_idx].items;

        // Symmetry breaking: canonicalise non-equipment ordering.
        if (child_items.size() > 2)
            std::sort(child_items.begin() + 1, child_items.end(),
                [](const Item& a, const Item& b) {
                    if (a.ppn != b.ppn) return a.ppn < b.ppn;
                    return a.enchs.hash() < b.enchs.hash();
                });

        _stack.push_back({
            std::move(child_items), _stack[frame_idx].cost_so_far + step_cost,
            0, _current_steps.size(), {}, {}, 0, 0, false
        });
        _frame_pairs.emplace_back();

        _stack[frame_idx].has_backtrack = true;
    }
}
