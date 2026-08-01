#include "BBDpAlgorithm.h"
#include "common/utils/thread/ThreadPool.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert (Pareto domination + optional beam) ────────────────

void BBDpAlgorithm::Frontier::insert(ParetoEntry entry, int32_t beam_width) {
    const Item& item = entry.item;

    // Cross-ppn Pareto domination over the same (type, enchset):
    //   - an existing entry with ≤ ppn and ≤ cost dominates `entry` → drop it;
    //   - `entry` with ≤ ppn and ≤ cost dominates an existing entry → remove it.
    // Future merge cost depends only on (type, enchset, ppn), so the dominated
    // entry can never lead to a strictly better result.
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->item.type == item.type && it->item.enchs == item.enchs) {
            if (it->ppn <= entry.ppn && it->cost <= entry.cost)
                return;  // entry is dominated
            if (entry.ppn <= it->ppn && entry.cost <= it->cost) {
                it = entries.erase(it);  // entry dominates existing
                continue;
            }
        }
        ++it;
    }
    entries.push_back(std::move(entry));

    // Beam: keep only the `beam_width` cheapest entries (heuristic — loses
    // optimality for beam_width > 0, traded for speed on large N).
    if (beam_width > 0 && entries.size() > static_cast<size_t>(beam_width)) {
        std::sort(entries.begin(), entries.end(),
            [](const ParetoEntry& a, const ParetoEntry& b) { return a.cost < b.cost; });
        entries.resize(static_cast<size_t>(beam_width));
    }
}

// ─── canonicalize ─────────────────────────────────────────────────────────

void BBDpAlgorithm::canonicalize(std::vector<Item>& items) noexcept {
    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            auto order = [](ItemType t) -> uint8_t {
                switch (t) {
                    case ItemType::Equip: return 0;
                    case ItemType::Book:  return 1;
                    default:              return 2;
                }
            };
            uint8_t oa = order(a.type);
            uint8_t ob = order(b.type);
            if (oa != ob) return oa < ob;
            if (a.ppn != b.ppn) return a.ppn < b.ppn;
            return a.enchs.hash() < b.enchs.hash();
        });
}

// ─── compute_ub: deterministic balanced-merge bound (hamming-style) ───────

static int popcount32(int x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(static_cast<unsigned>(x));
#else
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
#endif
}

static std::vector<int> dup_floor_members(int j, int n) {
    std::vector<int> result;
    result.reserve(static_cast<size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i)
        if (popcount32(i) == j) result.push_back(i);
    return result;
}

int32_t BBDpAlgorithm::compute_ub(std::vector<Item> items) {
    // Deterministic hamming-style construction: at each PPN tier, sort by
    // estimated forge cost desc (expensive books merge early), arrange into a
    // popcount-balanced bracket, pairwise-forge, bubble results to the next
    // tier.  Mirrors HammingAlgorithm's tier logic to produce a valid complete
    // solution whose total cost serves as the B&B upper bound.  Records the
    // merge steps into _ub_steps (used as an anytime fallback).  Returns
    // INT32_MAX when the construction does not reach the target.
    _ub_steps.clear();
    normalize_base_equipment(items);
    if (items.empty()) return INT32_MAX;

    int max_ppn = 0;
    for (const auto& it : items) max_ppn = std::max<int>(max_ppn, it.ppn);

    std::vector<std::vector<Item>> tiers(static_cast<size_t>(max_ppn) + 1);
    for (auto& it : items) tiers[static_cast<size_t>(it.ppn)].push_back(std::move(it));

    int64_t total = 0;
    for (size_t tier = 0; tier < tiers.size(); ++tier) {
        if (tiers[tier].size() <= 1) {
            if (tiers[tier].size() == 1) {
                size_t nt = tier + 1;
                if (nt < tiers.size()) {
                    tiers[nt].push_back(std::move(tiers[tier][0]));
                    tiers[tier].clear();
                }
            }
            continue;
        }

        auto& cur = tiers[tier];
        std::sort(cur.begin(), cur.end(), [&](const Item& a, const Item& b) {
            bool a_eq = (a.type == ItemType::Equip);
            bool b_eq = (b.type == ItemType::Equip);
            if (a_eq != b_eq) return a_eq;
            return _forge_engine.estimate_forge_cost(a, a, *_ench_reg)
                 > _forge_engine.estimate_forge_cost(b, b, *_ench_reg);
        });

        const size_t n = cur.size();
        std::vector<Item> arranged(n);
        arranged[0] = std::move(cur[0]);
        size_t src = 1;
        for (int j = 1; src < n; ++j) {
            auto members = dup_floor_members(j, static_cast<int>(n));
            for (int pos : members) {
                if (pos == 0 || src >= n) continue;
                arranged[static_cast<size_t>(pos)] = std::move(cur[src++]);
            }
        }

        std::vector<Item> next_items;
        while (arranged.size() >= 2) {
            Item base = std::move(arranged.front());
            arranged.erase(arranged.begin());
            Item sac = std::move(arranged.front());
            arranged.erase(arranged.begin());

            if (!_forge_engine.is_forgeable(base, sac)) {
                if (_forge_engine.is_forgeable(sac, base)) {
                    std::swap(base, sac);
                } else {
                    next_items.push_back(std::move(base));
                    next_items.push_back(std::move(sac));
                    continue;
                }
            }
            Item base_before = base;
            Item sac_before  = sac;
            int32_t cost = _forge_engine.forge_into(base, sac, *_ench_reg);
            if (cost == INT32_MAX) return INT32_MAX;
            total += cost;
            if (total > INT32_MAX) total = INT32_MAX;
            _ub_steps.emplace_back(std::move(base_before), std::move(sac_before), base, cost);
            next_items.push_back(std::move(base));
        }
        if (arranged.size() == 1)
            next_items.push_back(std::move(arranged[0]));

        size_t nt = tier + 1;
        if (nt >= tiers.size()) tiers.resize(nt + 1);
        for (auto& it : next_items) tiers[nt].push_back(std::move(it));
    }

    // Only trust the bound if the construction actually reaches the target.
    bool found_equip = false;
    Item final_item;
    for (auto& tier : tiers) {
        for (auto& it : tier) {
            if (it.type == ItemType::Equip) {
                final_item = std::move(it);
                found_equip = true;
            }
        }
    }
    if (!found_equip || !meets_target(final_item, _target))
        return INT32_MAX;
    return static_cast<int32_t>(total);
}

// ─── solve: recursive partition DP with B&B + cap + Pareto ───────────────

BBDpAlgorithm::Frontier BBDpAlgorithm::solve(std::vector<Item> items,
                                             int32_t max_step_cost,
                                             int32_t bound,
                                             int32_t beam_width,
                                             bool parallelize,
                                             bool final_level,
                                             ExecutionContext &ctx) {
    if (items.size() == 1) {
        Frontier f;
        f.entries.push_back(ParetoEntry{0, items[0].ppn, 0, std::move(items[0]), StepTree{}});
        return f;
    }

    if (items.size() == 2) {
        Frontier f;
        const Item& a = items[0];
        const Item& b = items[1];
        auto make_leaf = [&](const Item& base, const Item& sac,
                             const Item& result, int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, result, cost}, nullptr, nullptr, 1);
            return StepTree{std::move(node)};
        };
        auto try_forge = [&](const Item& base, const Item& sac) {
            if (!_forge_engine.is_forgeable(base, sac)) return;
            auto [result, cost] = _forge_engine.forge(base, sac, *_ench_reg);
            ctx.incr_steps_forged();
            if (cost == INT32_MAX) return;
            if (max_step_cost > 0 && cost > max_step_cost) return;
            if (static_cast<int64_t>(cost) > bound) return;  // keep == bound (may be optimal)
            auto tree = make_leaf(base, sac, result, cost);
            f.insert(ParetoEntry{cost, result.ppn, cost,
                                 std::move(result), std::move(tree)}, beam_width);
        };
        try_forge(a, b);
        try_forge(b, a);
        return f;
    }

    // Memoisation (frontiers pruned at this run's constant `bound`).
    {
        std::shared_lock lock(_cache_mutex);
        auto it = _cache.find(items);
        if (it != _cache.end())
            return it->second;
    }

    const size_t n = items.size();
    if (n > 28) return {};
    if (ctx.is_cancelled()) return {};

    Frontier result;

    auto combine = [&](Frontier& local, const ParetoEntry& a, const ParetoEntry& b) {
        if (a.cost + b.cost > bound) return;  // any completion > bound can't improve
        auto make_tree = [&](const std::shared_ptr<StepTree::Node>& base_tree,
                             const std::shared_ptr<StepTree::Node>& sac_tree,
                             const Item& base, const Item& sac,
                             const Item& result, int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, result, cost}, base_tree, sac_tree,
                a.step_tree.size() + b.step_tree.size() + 1);
            return StepTree{std::move(node)};
        };
        auto try_forge = [&](const Item& base, const Item& sac,
                             const std::shared_ptr<StepTree::Node>& base_tree,
                             const std::shared_ptr<StepTree::Node>& sac_tree) {
            if (!_forge_engine.is_forgeable(base, sac)) return;
            auto [new_item, cost] = _forge_engine.forge(base, sac, *_ench_reg);
            ctx.incr_steps_forged();
            if (cost == INT32_MAX) return;
            if (max_step_cost > 0 && cost > max_step_cost) return;
            int64_t total = a.cost + b.cost + cost;
            if (total > bound) return;  // keep == bound (may be optimal)
            int32_t max_step = std::max(a.max_step, b.max_step);
            max_step = std::max(max_step, cost);
            auto tree = make_tree(base_tree, sac_tree, base, sac, new_item, cost);
            local.insert(ParetoEntry{total, new_item.ppn, max_step,
                                     std::move(new_item), std::move(tree)},
                         beam_width);
        };
        try_forge(a.item, b.item, a.step_tree.root_ptr(), b.step_tree.root_ptr());
        try_forge(b.item, a.item, b.step_tree.root_ptr(), a.step_tree.root_ptr());
    };

    // Partition enumeration over all masks (each unordered 2-partition appears
    // once: keep masks with |left| ≤ n/2; for even n + equal halves require
    // bit 0 set).  The top level (n ≥ threshold) is parallelised over the mask
    // range; the recursion is always sequential.
    const uint64_t limit = 1ULL << n;

    auto process_mask = [&](Frontier& out, uint64_t mask) {
        ctx.wait_if_paused();
        if (ctx.is_cancelled()) return;
        const size_t k = __builtin_popcountll(mask);
        if (k * 2 > n) return;
        if ((n & 1) == 0 && k * 2 == n && !(mask & 1)) return;

        std::vector<Item> left, right;
        left.reserve(n);
        right.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (mask & (1ULL << i))
                left.push_back(items[i]);
            else
                right.push_back(items[i]);
        }
        canonicalize(left);
        canonicalize(right);

        Frontier left_f = solve(std::move(left), max_step_cost, bound, beam_width,
                                /*parallelize=*/false, /*final_level=*/false, ctx);
        if (!left_f.empty()) {
            Frontier right_f = solve(std::move(right), max_step_cost, bound, beam_width,
                                     /*parallelize=*/false, /*final_level=*/false, ctx);
            if (!right_f.empty()) {
                for (const auto& ea : left_f.entries)
                    for (const auto& eb : right_f.entries)
                        combine(out, ea, eb);
            }
        }
    };

    if (parallelize && n >= PARALLEL_THRESHOLD) {
        auto& pool = besq::ThreadPool::shared();
        std::mutex result_mutex;
        ctx.report_progress(20, ProgressStatus::Exploring);
        parallel_for(pool, uint64_t{1}, limit, [&](uint64_t mask) {
            Frontier local;
            process_mask(local, mask);
            if (!local.empty()) {
                std::lock_guard<std::mutex> lk(result_mutex);
                for (auto& e : local.entries)
                    result.insert(std::move(e), beam_width);
            }
        });
    } else {
        for (uint64_t mask = 1; mask < limit; ++mask) {
            process_mask(result, mask);
            if ((mask & 0xFF) == 0)
                ctx.report_progress(5 + static_cast<uint8_t>(90 * mask / limit),
                                    ProgressStatus::Exploring);
        }
    }

    // Drop complete entries that cannot beat the bound (keep those equal to it).
    if (bound < INT32_MAX) {
        result.entries.erase(
            std::remove_if(result.entries.begin(), result.entries.end(),
                [bound](const ParetoEntry& e) { return e.cost > bound; }),
            result.entries.end());
    }

    // Admissible PPN lower-bound: a non-final entry must be merged at least
    // once more, and that next merge pays ≥ penalty(ppn), so an entry with
    // cost + penalty(ppn) > bound can never reach a solution ≤ bound.
    if (!final_level && bound < INT32_MAX) {
        result.entries.erase(
            std::remove_if(result.entries.begin(), result.entries.end(),
                [&](const ParetoEntry& e) {
                    int64_t forced = e.cost + _forge_engine.penalty_cost(e.item.ppn);
                    return forced > bound;
                }),
            result.entries.end());
    }

    {
        std::unique_lock lock(_cache_mutex);
        if (_cache.find(items) == _cache.end() && _cache.size() < MAX_CACHE_ENTRIES)
            _cache[std::move(items)] = result;
    }
    return result;
}

// ─── evaluate ─────────────────────────────────────────────────────────────

double BBDpAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // B&B bound + cap + Pareto collapse the Catalan blow-up; placeholder fit
    // (discounted from dp_merge's 2.9^e).  Recalibrate on the benchmark.
    return 0.002 * std::pow(2.5, static_cast<double>(ench_count));
}

// ─── execute ──────────────────────────────────────────────────────────────

void BBDpAlgorithm::execute(const AlgorithmInput &input, ExecutionContext &ctx) {
    _forge_engine.set_config(input.config.forge);
    _ench_reg = &input.registry;
    _target.clear();
    for (const auto& e : input.target.enchs)
        _target.push_back(e);
    _diag = AlgorithmDiagnostics{};

    // Configure thread-pool concurrency from search config.
    if (input.config.search.max_threads > 0)
        besq::ThreadPool::set_concurrency(input.config.search.max_threads);

    ctx.report_progress(0, ProgressStatus::Starting);

    auto items = get_resolver()->resolve(input);
    normalize_base_equipment(items);

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

    const int32_t cap        = input.config.search.max_step_cost;
    const int32_t beam_width = input.config.search.beam_width;

    // Upper bound: the internal hamming-style construction (also fills
    // _ub_steps for the anytime fallback).  A warm-up SearchConfig.initial_bound
    // is only trusted when it is ≥ this achievable cost — a user-supplied bound
    // below the optimum would prune it and wrongly yield "no solution".
    int32_t internal = compute_ub(items);
    const bool ub_ok = (internal != INT32_MAX);  // construction reached the target
    int32_t bound = input.config.search.initial_bound;
    if (bound <= 0 || bound == INT32_MAX) {
        bound = internal;
    } else if (internal != INT32_MAX) {
        bound = std::max(bound, internal);
    }
    if (bound < INT32_MAX) {
        // Tighten with a node-limited DFS (always to an achievable cost ≥ optimum).
        int64_t node_limit = 50'000;
        std::vector<int16_t> h_buf, h_dirty;
        bound = search_utils::dfs_bound(items, 0, bound, node_limit,
                                        _forge_engine, *_ench_reg, _target,
                                        h_buf, h_dirty);
    }

    canonicalize(items);

    auto find_best = [&](const Frontier& f) -> const ParetoEntry* {
        const ParetoEntry* best = nullptr;
        for (const auto& e : f.entries)
            if (e.item.type == ItemType::Equip && meets_target(e.item, _target))
                if (!best || e.cost < best->cost)
                    best = &e;
        return best;
    };

    // ── Fast path: unconstrained search with the (tight) upper bound ──────
    _cache.clear();
    Frontier frontier = solve(items, 0, bound, beam_width,
                              /*parallelize=*/true, /*final_level=*/true, ctx);
    if (ctx.is_cancelled()) {
        _diag.status = "Cancelled";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::Cancelled);
        return;
    }
    const ParetoEntry* best = find_best(frontier);

    if (cap > 0 && best && best->max_step > cap) {
        // The pure optimum violates the per-step cap: find the best fully
        // ≤cap solution.  Seed the B&B bound with the cheapest ≤cap entry the
        // unconstrained search already saw (sound: that cost is achievable and
        // feasible); fall back to unbound if none was seen.
        const ParetoEntry* feasible_ub = nullptr;
        for (const auto& e : frontier.entries)
            if (e.item.type == ItemType::Equip && meets_target(e.item, _target)
                && e.max_step <= cap)
                if (!feasible_ub || e.cost < feasible_ub->cost)
                    feasible_ub = &e;
        int32_t feas_bound = feasible_ub ? static_cast<int32_t>(feasible_ub->cost) : INT32_MAX;

        _cache.clear();
        Frontier feas_frontier = solve(items, cap, feas_bound, beam_width,
                                       /*parallelize=*/true, /*final_level=*/true, ctx);
        if (ctx.is_cancelled()) {
            _diag.status = "Cancelled";
            ctx.set_exit_diagnostics(_diag);
            ctx.report_progress(100, ProgressStatus::Cancelled);
            return;
        }
        const ParetoEntry* feas_best = find_best(feas_frontier);
        if (feas_best)
            best = feas_best;  // prefer the fully-feasible solution
    }

    if (best) {
        _diag.solution_cost = static_cast<int32_t>(best->cost);
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(best->step_tree.materialize());
        ctx.report_progress(100, ProgressStatus::Complete);
    } else if (ub_ok && !_ub_steps.empty()) {
        // Anytime fallback: the exact search gave up (e.g. n > 28 or a search
        // failure), but the deterministic hamming construction reached the
        // target — report it rather than "no solution".
        int64_t ub_cost = 0;
        for (const auto& s : _ub_steps) ub_cost += s.cost;
        _diag.solution_cost = static_cast<int32_t>(std::min<int64_t>(ub_cost, INT32_MAX));
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(_ub_steps);
        ctx.report_progress(100, ProgressStatus::Complete);
    } else {
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
    }
}

} // namespace algorithm
