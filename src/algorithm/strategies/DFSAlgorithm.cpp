#include "DFSAlgorithm.h"
#include "../ExecutionContext.h"
#include "utils/CompactForgeUtils.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;
using compact::estimate_forge_cost;
using compact::book_multiplier;

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
    auto& forge = const_cast<ForgeEngine&>(_compact_forge);

    std::vector<std::pair<size_t, int32_t>> ordered;
    for (size_t i = 0; i < books.size(); ++i)
        ordered.emplace_back(i, estimate_forge_cost(equip, books[i], reg));
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& [idx, _] : ordered) {
        if (!_compact_forge.is_forgeable(equip, books[idx]))
            continue;
        int32_t cost = forge.forge_into(equip, books[idx], reg);
        total_cost += cost;
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
            if (!_compact_forge.is_forgeable(items[i], items[j]))
                continue;

            int32_t est = estimate_forge_cost(items[i], items[j], *_ench_reg);
            pairs.push_back({i, j, est});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const ForgePair& a, const ForgePair& b) { return a.est_cost < b.est_cost; });
    return pairs;
}

// ─── Hot-path helpers ──────────────────────────────────────────────────────

bool DFSAlgorithm::_meets_target(const Item& equipment) const {
    for (const auto& t : _target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

int32_t DFSAlgorithm::_heuristic(const std::vector<Item>& items) const {
    int32_t h = 0;
    if (items.empty()) return h;

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
            int32_t bm = book_multiplier(_ench_reg->get_multiplier(t.id));
            h += (t.level - have) * bm;
        }
    }
    return h;
}

// ─── execute ───────────────────────────────────────────────────────────────

void DFSAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    _ench_reg = &reg;
    _target = target;

    _best_cost = INT32_MAX;
    _best_steps.clear();
    _current_steps.clear();
    _visited.clear();
    _stack.clear();
    _frame_pairs.clear();
    _solutions_found = 0;

    if (items.size() > 1)
        _best_cost = _greedy_bound(items, reg);

    _stack.push_back({items, 0, 0, 0, {}, {}, 0, 0, false});
    _frame_pairs.emplace_back();

    _dfs_iterative(ctx);

    ctx.report_progress(1.0, ProgressStatus::Complete);
}

// ─── Iterative search ──────────────────────────────────────────────────────

void DFSAlgorithm::_dfs_iterative(ExecutionContext& ctx) {
    while (!_stack.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        auto& frame = _stack.back();

        if (frame.has_backtrack) {
            size_t adj_base = (frame.sac_idx < frame.base_idx)
                ? frame.base_idx - 1 : frame.base_idx;
            frame.items[adj_base] = std::move(frame.saved_base);
            frame.items.insert(
                frame.items.begin() + frame.sac_idx, std::move(frame.saved_sac));
            _current_steps.resize(frame.saved_steps_size);
            frame.has_backtrack = false;
        }

        {
            auto [it, inserted] = _visited.insert(frame.items);
            if (!inserted) {
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
        }

        if (_meets_target(frame.items[0])) {
            ++_solutions_found;
            ctx.report_compact_solution(_current_steps);

            if (_best_steps.empty() || frame.cost_so_far < _best_cost) {
                _best_cost = frame.cost_so_far;
                _best_steps = _current_steps;
            }
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        if (frame.cost_so_far + _heuristic(frame.items) >= _best_cost) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

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

        auto& pairs = _frame_pairs.back();
        if (pairs.empty())
            pairs = _collect_pairs(frame.items);

        if (frame.pair_index >= pairs.size()) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        const auto& p = pairs[frame.pair_index++];

        frame.saved_base = frame.items[p.i];
        frame.saved_sac = frame.items[p.j];
        frame.base_idx = p.i;
        frame.sac_idx = p.j;

        int32_t step_cost = _compact_forge.forge_into(
            frame.items[p.i], frame.items[p.j], *_ench_reg);

        _current_steps.push_back({
            frame.saved_base, frame.saved_sac, step_cost
        });

        frame.items.erase(frame.items.begin() + p.j);

        std::vector<compact::Item> child_items = frame.items;

        _stack.push_back({
            std::move(child_items), frame.cost_so_far + step_cost,
            0, _current_steps.size(), {}, {}, 0, 0, false
        });
        _frame_pairs.emplace_back();

        frame.has_backtrack = true;
    }
}
