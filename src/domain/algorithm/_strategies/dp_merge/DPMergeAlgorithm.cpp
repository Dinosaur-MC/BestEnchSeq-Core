#include "DPMergeAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert ──────────────────────────────────────────────────────
//
// Bucket by (EnchSet, PPN) equivalence class.  Items with identical enchants
// at the same PPN are strictly interchangeable for any future forge:
//   - Forge cost contribution of sacrifice = Σ(ench × mul_b) + 2^ppn − 1
//     (the penalty part is identical for same PPN; the ench part is identical
//      for same EnchSet)
//   - All enchants transfer to the base (modulo incompatibility, which is
//     also identical for the same EnchSet)
//
// Therefore only the cheapest entry per (EnchSet, PPN) pair needs to survive.

void DPMergeAlgorithm::Frontier::insert(ParetoEntry entry) {
    for (auto& existing : entries) {
        if (existing.ppn == entry.ppn && existing.item.enchs == entry.item.enchs) {
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
            // Equip before Book before Material
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
    // ── Base case: single item ─────────────────────────────────────
    if (items.size() == 1) {
        Frontier f;
        f.entries.push_back(ParetoEntry{0, items[0].ppn, std::move(items[0]), {}});
        return f;
    }

    // ── Base case: two items — try both forge directions ───────────
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

    // ── Partition enumeration (recursive case) ──────────────────────
    //
    // Enumerate all 2-partitions via bitmask.  mask bit i == 1 means
    // items[i] goes to the left group.  For each mask, recursively solve
    // both halves and combine their Pareto frontiers via forge_into.
    //
    // To avoid symmetric duplicates we only keep masks with
    // popcount ≤ n/2.  For even n and popcount == n/2 we additionally
    // require bit 0 set (first item in left group).

    const size_t n = items.size();

    // Safety guard — DP is impractical for n > 20
    if (n > 20) {
        _cache[std::move(items)] = Frontier{};
        return {};
    }

    Frontier result;
    const uint64_t limit = 1ULL << n;

    // Reusable buffers (avoid reallocation per mask)
    std::vector<Item> left_buf, right_buf;
    left_buf.reserve(n);
    right_buf.reserve(n);

    for (uint64_t mask = 1; mask + 1 < limit; ++mask) {
        size_t k = __builtin_popcountll(mask);

        // Symmetry reduction: only keep masks with popcount ≤ n/2
        if (k * 2 > n) continue;
        // For even n and k == n/2: enforce first item in left group
        if ((n & 1) == 0 && k * 2 == n && !(mask & 1)) continue;

        left_buf.clear();
        right_buf.clear();

        for (size_t i = 0; i < n; ++i) {
            if (mask & (1ULL << i))
                left_buf.push_back(items[i]);
            else
                right_buf.push_back(items[i]);
        }

        // Canonicalise before recursive calls (cache key requirement)
        canonicalize(left_buf);
        canonicalize(right_buf);

        Frontier left_f  = solve(std::move(left_buf));
        Frontier right_f = solve(std::move(right_buf));

        if (left_f.empty() || right_f.empty())
            continue;

        // ── Combine sub-results via Cartesian product ────────────
        for (const auto& entry_a : left_f.entries) {
            for (const auto& entry_b : right_f.entries) {
                // a → b
                if (_forge_engine.is_forgeable(entry_a.item, entry_b.item)) {
                    auto [new_item, cost] = _forge_engine.forge(
                        entry_a.item, entry_b.item, *_ench_reg);
                    int32_t total = entry_a.cost + entry_b.cost + cost;

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

                // b → a
                if (_forge_engine.is_forgeable(entry_b.item, entry_a.item)) {
                    auto [new_item, cost] = _forge_engine.forge(
                        entry_b.item, entry_a.item, *_ench_reg);
                    int32_t total = entry_a.cost + entry_b.cost + cost;

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

    // ── Store in cache ──────────────────────────────────────────────
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

    // ── Guard: empty input ───────────────────────────────────────────
    if (items.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    // ── Guard: target already met ────────────────────────────────────
    if (meets_target(items[0], _target)) {
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        _diag.status = "GoalAlreadyMet";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    // ── Guard: no books to work with ────────────────────────────────
    if (items.size() <= 1) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    // ── Run DP ───────────────────────────────────────────────────────
    std::vector<Item> mutable_items = items;
    canonicalize(mutable_items);

    Frontier frontier = solve(std::move(mutable_items));

    // ── Find cheapest entry that meets target ────────────────────────
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
        _diag.solution_cost = best->cost;
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

} // namespace algorithm
