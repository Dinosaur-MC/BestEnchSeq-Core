#include "CompactDFSAlgorithm.h"
#include "utils/CompactAdapter.hpp"
#include "utils/AlgorithmUtils.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

// ─── Collect forge pairs ───────────────────────────────────────────────────

std::vector<CompactDFSAlgorithm::ForgePair> CompactDFSAlgorithm::_collect_pairs(
    const std::vector<compact::Item>& items) const
{
    const size_t n = items.size();
    std::vector<ForgePair> pairs;
    pairs.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!compact::CompactForgeEngine::is_forgeable(items[i], items[j]))
                continue;

            int32_t est = compact::estimate_forge_cost(items[i], items[j], *_ench_reg);
            pairs.push_back({i, j, est});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const ForgePair& a, const ForgePair& b) { return a.est_cost < b.est_cost; });
    return pairs;
}

// ─── Hot-path helpers ──────────────────────────────────────────────────────

bool CompactDFSAlgorithm::_meets_target(const compact::Item& equipment) const {
    for (const auto& t : _target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

int32_t CompactDFSAlgorithm::_heuristic(const std::vector<compact::Item>& items) const {
    int32_t h = 0;
    if (items.empty()) return h;

    // Build max level per ench ID across all items (unordered_map for O(M*N + T)).
    std::unordered_map<int16_t, int16_t> max_levels;
    for (const auto& item : items) {
        for (const auto& e : item.enchs) {
            auto it = max_levels.find(e.id);
            if (it == max_levels.end())
                max_levels[e.id] = e.level;
            else if (e.level > it->second)
                it->second = e.level;
        }
    }

    for (const auto& t : _target) {
        auto it = max_levels.find(t.id);
        int16_t have = (it == max_levels.end()) ? 0 : it->second;
        if (have < t.level) {
            int32_t bm = compact::book_multiplier(_ench_reg->get_multiplier(t.id));
            h += (t.level - have) * bm;
        }
    }
    return h;
}

// ─── execute ───────────────────────────────────────────────────────────────

void CompactDFSAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    //── Boundary: prepare compact data ────────────────────────────────────
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(EnchantmentRegistry::get_instance(), *input.target_item.equipment);
    _ench_reg = &ench_reg;

    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;

    // Extract target enchantments in compact form
    _target.clear();
    _target.reserve(input.target_item.enchantments.size());
    for (const auto& e : input.target_item.enchantments)
        _target.push_back({static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});

    // Reset state
    _best_cost = INT32_MAX;
    _best_steps.clear();
    _current_steps.clear();
    _visited.clear();
    _stack.clear();
    _frame_pairs.clear();
    _solutions_found = 0;

    // Greedy upper bound for pruning (uses domain engine, done once at boundary)
    if (items.size() > 1) {
        auto bound = AlgorithmUtils::book_first_merge(
            input.target_item, input.available_items, _bound_engine, ctx);
        _best_cost = bound.total_cost;
    }

    // Push root frame
    _stack.push_back({std::move(items), 0, 0, 0, {}, {}, 0, 0, false});
    _frame_pairs.emplace_back();

    // Run iterative search (compact-only)
    _dfs_iterative(ctx, ci.equipment);

    ctx.report_progress(1.0, ProgressStatus::Complete);
}

// ─── Iterative search (compact-only, no domain deps) ───────────────────────

void CompactDFSAlgorithm::_dfs_iterative(ExecutionContext& ctx, const Equipment* out_eq) {
    while (!_stack.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        auto& frame = _stack.back();

        // 1. Handle backtrack restore
        if (frame.has_backtrack) {
            size_t adj_base = (frame.sac_idx < frame.base_idx)
                ? frame.base_idx - 1 : frame.base_idx;
            frame.items[adj_base] = std::move(frame.saved_base);
            frame.items.insert(
                frame.items.begin() + frame.sac_idx, std::move(frame.saved_sac));
            _current_steps.resize(frame.saved_steps_size);
            frame.has_backtrack = false;
        }

        // 2. State memoization
        {
            auto [it, inserted] = _visited.insert(frame.items);
            if (!inserted) {
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
        }

        // 3. Goal check (compact only)
        if (_meets_target(frame.items[0])) {
            ++_solutions_found;

            //── Boundary: convert compact steps to domain for reporting ──
            auto domain_steps = compact::to_domain(
                _current_steps.begin(), _current_steps.end(), out_eq);
            ctx.report_solution_found(domain_steps);

            if (_best_steps.empty() || frame.cost_so_far < _best_cost) {
                _best_cost = frame.cost_so_far;
                _best_steps = std::move(domain_steps);
            }
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // 4. Branch-and-bound pruning (compact heuristic)
        if (frame.cost_so_far + _heuristic(frame.items) >= _best_cost) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // 5. Hot-update config check
        {
            auto cfg = ctx.get_search_config();
            if (cfg.max_depth > 0 &&
                static_cast<int32_t>(_stack.size()) > cfg.max_depth) {
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
            if (cfg.max_solutions > 0 && _solutions_found >= cfg.max_solutions)
                break;
        }

        // 6. Lazy pair building
        auto& pairs = _frame_pairs.back();
        if (pairs.empty()) {
            pairs = _collect_pairs(frame.items);
        }

        // 7. Find next valid pair
        if (frame.pair_index >= pairs.size()) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // 8. Execute forge and push child frame
        const auto& p = pairs[frame.pair_index++];

        frame.saved_base = frame.items[p.i];
        frame.saved_sac = frame.items[p.j];
        frame.base_idx = p.i;
        frame.sac_idx = p.j;

        int32_t step_cost = _compact_forge.forge_into(
            frame.items[p.i], frame.items[p.j], *_ench_reg);

        // Record step (compact — no domain conversion)
        _current_steps.push_back({
            frame.saved_base,
            frame.saved_sac,
            step_cost
        });

        frame.items.erase(frame.items.begin() + p.j);

        std::vector<compact::Item> child_items = frame.items;

        _stack.push_back({
            std::move(child_items),
            frame.cost_so_far + step_cost,
            0,
            _current_steps.size(),
            {}, {}, 0, 0, false
        });
        _frame_pairs.emplace_back();

        frame.has_backtrack = true;
    }
}
