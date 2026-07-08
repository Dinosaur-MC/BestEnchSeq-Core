#include "DFSAlgorithm.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/ExpCalculator.hpp"
#include "utils/Serializer.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

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

        // Canonical order: sort enchantments by id.
        // EnchSet is std::unordered_set — iteration order is NOT deterministic,
        // so we must sort to produce a consistent state key for memoization.
        std::vector<Ench> sorted(item.enchantments.begin(), item.enchantments.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Ench& a, const Ench& b) { return a.id < b.id; });
        for (const Ench& e : sorted) {
            key.ench_ids.push_back(e.id);
            key.ench_levels.push_back(e.level);
        }
    }

    return key;
}

void DFSAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);

    _input = &input;

    if (!_state_restored) {
        // Fresh start: initialize all state, compute greedy upper bound.
        _best_cost   = INT32_MAX;
        _best_steps.clear();
        _current_steps.clear();
        _visited.clear();

        // Build initial items
        ItemStack start_item(
            input.target_item.equipment,
            input.original_ench,
            0
        );
        std::vector<ItemStack> items;
        items.reserve(1 + input.available_items.size());
        items.push_back(start_item);
        items.insert(items.end(), input.available_items.begin(), input.available_items.end());

        // Greedy upper bound for pruning
        if (items.size() > 1) {
            auto bound = AlgorithmUtils::book_first_merge(
                items[0], input.available_items, _forge_engine, ctx);
            _best_cost = bound.total_cost;
        }

        _state_restored = false;

        // Launch recursive DFS
        dfs(items, 0, ctx);
    } else {
        // Restored from checkpoint: _best_cost, _best_steps, _visited are
        // already populated by deserialize_state(). Only clear current path.
        _current_steps.clear();
        _state_restored = false;

        // Rebuild initial items from the input
        ItemStack start_item(
            input.target_item.equipment,
            input.original_ench,
            0
        );
        std::vector<ItemStack> items;
        items.reserve(1 + input.available_items.size());
        items.push_back(start_item);
        items.insert(items.end(), input.available_items.begin(), input.available_items.end());

        // Launch DFS with existing _best_cost as the bound
        dfs(items, 0, ctx);
    }

    // Report the best solution found
    if (!_best_steps.empty()) {
        ctx.report_solution_found(_best_steps);
    }

    ctx.report_progress(1.0, ProgressStatus::Complete);
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

std::vector<uint8_t> DFSAlgorithm::serialize_state() const {
    Serializer s;

    // Magic header
    s.u32(Serializer::MAGIC);
    s.u32(Serializer::VERSION);

    // Serialize algorithm name for identification
    s.string(name());

    // Best cost and best steps
    s.i32(_best_cost);
    s.u32(static_cast<uint32_t>(_best_steps.size()));
    for (const auto& step : _best_steps)
        s.write(step);

    // Visited state keys
    s.u32(static_cast<uint32_t>(_visited.size()));
    for (const auto& key : _visited) {
        s.write(key.penalties);
        s.write(key.ench_ids);
        s.write(key.ench_levels);
    }

    return s.data();
}

void DFSAlgorithm::deserialize_state(const std::vector<uint8_t>& data) {
    Deserializer d(data);

    // Validate magic
    uint32_t magic = d.u32();
    if (magic != Serializer::MAGIC || !d.ok())
        return;  // Invalid data -- silently ignore

    uint32_t version = d.u32();
    (void)version;  // Reserved for future format migration

    std::string algo_name = d.string();
    if (algo_name != name() || !d.ok())
        return;  // Wrong algorithm type -- silently ignore

    // Read best cost
    _best_cost = d.i32();

    // Read best steps
    uint32_t step_count = d.u32();
    _best_steps.clear();
    _best_steps.reserve(step_count);
    for (uint32_t i = 0; i < step_count; ++i) {
        if (!d.ok()) break;
        _best_steps.push_back(d.read_step());
    }

    // Read visited state keys
    uint32_t visit_count = d.u32();
    _visited.clear();
    _visited.reserve(visit_count);
    for (uint32_t i = 0; i < visit_count; ++i) {
        if (!d.ok()) break;
        StateKey key;
        key.penalties = d.read_i32_vec();
        key.ench_ids = d.read_i32_vec();
        key.ench_levels = d.read_i32_vec();
        _visited.insert(std::move(key));
    }

    if (d.ok())
        _state_restored = true;
}
