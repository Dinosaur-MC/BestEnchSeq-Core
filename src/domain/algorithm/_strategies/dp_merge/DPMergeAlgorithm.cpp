#include "DPMergeAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert ──────────────────────────────────────────────────────

void DPMergeAlgorithm::Frontier::insert(ParetoEntry entry) {
    for (auto& existing : entries) {
        if (existing.ppn == entry.ppn &&
            existing.item.type == entry.item.type &&
            existing.item.enchs == entry.item.enchs) {
            if (entry.cost < existing.cost) {
                existing = std::move(entry);
            }
            return;
        }
    }
    entries.push_back(std::move(entry));
}

// ─── canonicalize ──────────────────────────────────────────────────────────

void DPMergeAlgorithm::canonicalize(std::vector<Item>& items) noexcept {
    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            auto order = [](ItemType t) -> uint8_t {
                switch (t) {
                    case ItemType::Equip:  return 0;
                    case ItemType::Book:   return 1;
                    default:               return 2;
                }
            };
            uint8_t oa = order(a.type);
            uint8_t ob = order(b.type);
            if (oa != ob) return oa < ob;
            if (a.ppn != b.ppn) return a.ppn < b.ppn;
            return a.enchs.hash() < b.enchs.hash();
        });
}

// ─── solve (recursive DP with memoization) ─────────────────────────────────

DPMergeAlgorithm::Frontier DPMergeAlgorithm::solve(std::vector<Item> items) {
    if (items.size() == 1) {
        Frontier f;
        f.entries.push_back(ParetoEntry{0, items[0].ppn, std::move(items[0]), {}});
        return f;
    }

    if (items.size() == 2) {
        Frontier f;
        const Item& a = items[0];
        const Item& b = items[1];

        if (_forge_engine.is_forgeable(a, b)) {
            auto [result, cost] = _forge_engine.forge(a, b, *_ench_reg);
            std::vector<EnchStep> steps;
            steps.emplace_back(a, b, cost);
            f.insert(ParetoEntry{cost, result.ppn, std::move(result), std::move(steps)});
        }
        if (_forge_engine.is_forgeable(b, a)) {
            auto [result, cost] = _forge_engine.forge(b, a, *_ench_reg);
            std::vector<EnchStep> steps;
            steps.emplace_back(b, a, cost);
            f.insert(ParetoEntry{cost, result.ppn, std::move(result), std::move(steps)});
        }
        return f;
    }

    // ── Check memoisation cache ────────────────────────────────────
    {
        auto it = _cache.find(items);
        if (it != _cache.end())
            return it->second;
    }

    const size_t n = items.size();

    if (n >= 64) return {};
    if (n > 20) {
        _cache[std::move(items)] = Frontier{};
        return {};
    }

    Frontier result;
    const uint64_t limit = 1ULL << n;

    std::vector<Item> left_buf, right_buf;
    left_buf.reserve(n);
    right_buf.reserve(n);

    for (uint64_t mask = 1; mask + 1 < limit; ++mask) {
        size_t k = __builtin_popcountll(mask);

        if (k * 2 > n) continue;
        if ((n & 1) == 0 && k * 2 == n && !(mask & 1)) continue;

        left_buf.clear();
        right_buf.clear();

        for (size_t i = 0; i < n; ++i) {
            if (mask & (1ULL << i))
                left_buf.push_back(items[i]);
            else
                right_buf.push_back(items[i]);
        }

        canonicalize(left_buf);
        canonicalize(right_buf);

        Frontier left_f  = solve(std::move(left_buf));
        Frontier right_f = solve(std::move(right_buf));

        if (left_f.empty() || right_f.empty())
            continue;

        for (const auto& entry_a : left_f.entries) {
            for (const auto& entry_b : right_f.entries) {
                if (_forge_engine.is_forgeable(entry_a.item, entry_b.item)) {
                    auto [new_item, cost] = _forge_engine.forge(
                        entry_a.item, entry_b.item, *_ench_reg);
                    // Use int64_t for accumulation to prevent overflow;
                    // saturate to INT32_MAX for solution output.
                    int64_t total = static_cast<int64_t>(entry_a.cost)
                                  + entry_b.cost + cost;
                    if (total > std::numeric_limits<int32_t>::max())
                        total = std::numeric_limits<int32_t>::max();

                    std::vector<EnchStep> steps;
                    steps.reserve(entry_a.steps.size() +
                                  entry_b.steps.size() + 1);
                    steps.insert(steps.end(),
                                 entry_a.steps.begin(), entry_a.steps.end());
                    steps.insert(steps.end(),
                                 entry_b.steps.begin(), entry_b.steps.end());
                    steps.emplace_back(entry_a.item, entry_b.item, cost);

                    result.insert(ParetoEntry{total, new_item.ppn,
                                              std::move(new_item),
                                              std::move(steps)});
                }

                if (_forge_engine.is_forgeable(entry_b.item, entry_a.item)) {
                    auto [new_item, cost] = _forge_engine.forge(
                        entry_b.item, entry_a.item, *_ench_reg);
                    int64_t total = static_cast<int64_t>(entry_a.cost)
                                  + entry_b.cost + cost;
                    if (total > std::numeric_limits<int32_t>::max())
                        total = std::numeric_limits<int32_t>::max();

                    std::vector<EnchStep> steps;
                    steps.reserve(entry_a.steps.size() +
                                  entry_b.steps.size() + 1);
                    steps.insert(steps.end(),
                                 entry_a.steps.begin(), entry_a.steps.end());
                    steps.insert(steps.end(),
                                 entry_b.steps.begin(), entry_b.steps.end());
                    steps.emplace_back(entry_b.item, entry_a.item, cost);

                    result.insert(ParetoEntry{total, new_item.ppn,
                                              std::move(new_item),
                                              std::move(steps)});
                }
            }
        }
    }

    // ── Store in cache (limit size to prevent heap exhaustion) ─────
    if (_cache.size() < MAX_CACHE_ENTRIES)
        _cache[std::move(items)] = result;
    return result;
}

// ─── execute ──────────────────────────────────────────────────────────────

void DPMergeAlgorithm::execute(const AlgorithmInput& input,
                                ExecutionContext& ctx) {
    _forge_engine.set_config(input.f_config);
    _ench_reg  = &input.ench_reg;
    _target.clear();
    for (const auto& e : input.target.enchs)
        _target.push_back(e);

    _cache.clear();
    _diag = AlgorithmDiagnostics{};

    ctx.report_progress(0, ProgressStatus::Starting);

    const auto& items = input.items;

    if (items.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    if (meets_target(items[0], _target)) {
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        _diag.status = "GoalAlreadyMet";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    if (items.size() <= 1) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    std::vector<Item> mutable_items = items;
    canonicalize(mutable_items);

    Frontier frontier = solve(std::move(mutable_items));

    const ParetoEntry* best = nullptr;
    for (const auto& entry : frontier.entries) {
        if (entry.item.type == ItemType::Equip &&
            meets_target(entry.item, _target))
        {
            if (!best || entry.cost < best->cost)
                best = &entry;
        }
    }

    if (best) {
        _diag.solution_cost = static_cast<int32_t>(best->cost);
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(best->steps);
        ctx.report_progress(100, ProgressStatus::Complete);
    } else {
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
    }
}


// ─── evaluate ──────────────────────────────────────────────────────────────────

double DPMergeAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // Fitted from benchmark data:  t(e) ≈ 0.004 × 2.7^e
    // (Catalan-number DP, confirmed 1ms@7 / 20ms@9 / 389ms@12 / 14s@16)
    double r = 0.004 * std::pow(2.7, static_cast<double>(ench_count));
    return r;
}

// ─── process ───────────────────────────────────────────────────────────────────

std::optional<Item> DPMergeAlgorithm::process(const EnchSolution &solution) const {
    if (solution.steps.empty())
        return std::nullopt;
    if (!_ench_reg)
        return std::nullopt;

    // Replay forge steps sequentially to compute the final item.
    // When a step's base is equipment, switch to it — books are intermediate.
    Item result = solution.steps[0].base;
    for (const auto &step : solution.steps) {
        if (step.base.type == ItemType::Equip)
            result = step.base;
        _forge_engine.forge_into(result, step.sacrifice, *_ench_reg);
    }
    return result;
}

} // namespace algorithm
