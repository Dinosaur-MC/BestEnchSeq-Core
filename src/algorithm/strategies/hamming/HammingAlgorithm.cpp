#include "algorithm/strategies/hamming/HammingAlgorithm.h"
#include "algorithm/ExecutionContext.h"
#include "algorithm/components/SearchUtils.h"
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace compact;

// ─── Static helpers ─────────────────────────────────────────────────────────

std::vector<int> HammingAlgorithm::dup_floor_members(int j, int n) noexcept {
    std::vector<int> result;
    result.reserve(static_cast<size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i) {
        if (popcount(i) == j)
            result.push_back(i);
    }
    return result;
}

// ─── Popcount arrangement ──────────────────────────────────────────────────

void HammingAlgorithm::arrange_by_popcount(
    std::vector<Item>& items,
    const EnchReg& reg) const
{
    const size_t n = items.size();
    if (n <= 2) return;

    // Sort by forge cost descending (expensive books first → merge early).
    // Equipment unconditionally goes to position 0.
    std::sort(items.begin(), items.end(),
        [&](const Item& a, const Item& b) {
            bool a_eq = (a.type == ItemType::Equip);
            bool b_eq = (b.type == ItemType::Equip);
            if (a_eq != b_eq) return a_eq;
            return _forge_engine.estimate_forge_cost(a, a, reg)
                 > _forge_engine.estimate_forge_cost(b, b, reg);
        });

    // Popcount-balanced arrangement.
    // Loop j over popcount levels until all items are placed.
    std::vector<Item> arranged(n);
    arranged[0] = std::move(items[0]);

    size_t src = 1;
    for (int j = 1; src < n; ++j) {
        auto members = dup_floor_members(j, static_cast<int>(n));
        for (int pos : members) {
            if (pos == 0 || src >= n) continue;
            arranged[static_cast<size_t>(pos)] = std::move(items[src++]);
        }
    }

    items = std::move(arranged);
}

// ─── execute ───────────────────────────────────────────────────────────────

void HammingAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    ctx.report_progress(0, ProgressStatus::Starting);

    // ── Quick exit checks ──────────────────────────────────────────────

    if (input.items.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    if (meets_target(input.items[0], target)) {
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        return;
    }

    if (input.items.size() <= 1) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    // ── Phase 1: Seed the PPN-tiered work queue ──────────────────────────

    std::vector<Item> items(input.items.begin(), input.items.end());

    int max_ppn = 0;
    for (const auto& item : items)
        if (item.ppn > max_ppn) max_ppn = item.ppn;

    std::vector<std::vector<Item>> tiers(static_cast<size_t>(max_ppn) + 1);
    for (auto& item : items)
        tiers[item.ppn].push_back(std::move(item));

    std::vector<EnchStep> steps;
    steps.reserve(input.items.size() - 1);

    bool cancelled = false;

    // ── Phase 2: Bottom-up sequential-tier processing (the Hamming triangle) ──

    // Items bubble up one tier per outer iteration (tier → tier+1).
    // Forged results and leftovers always go to tier+1, guaranteeing every
    // item converges at the final tier.
    for (size_t tier = 0; tier < tiers.size() && !cancelled; ++tier) {
        ctx.wait_if_paused();
        if (ctx.is_cancelled()) { cancelled = true; break; }

        if (tiers[tier].size() <= 1) {
            // Single item: carry to next sequential tier, but only if
            // that tier exists.  If this is the last existing tier, the
            // item stays for the final scan.
            if (tiers[tier].size() == 1) {
                size_t nt = tier + 1;
                if (nt < tiers.size()) {
                    tiers[nt].push_back(std::move(tiers[tier][0]));
                    tiers[tier].clear();
                }
            }
            continue;
        }

        // Arrange items in this tier using popcount ordering
        arrange_by_popcount(tiers[tier], reg);

        // Pairwise forge: drain current tier, collect results in local buf.
        std::vector<Item> next_items;

        while (tiers[tier].size() >= 2 && !cancelled) {
            ctx.wait_if_paused();
            if (ctx.is_cancelled()) { cancelled = true; break; }

            // Pop base from front (analogous to old Qt takeFirst())
            Item base = std::move(tiers[tier].front());
            tiers[tier].erase(tiers[tier].begin());

            // Pop sacrifice from front (now at index 0 after erase)
            Item sac = std::move(tiers[tier].front());
            tiers[tier].erase(tiers[tier].begin());

            // ── Validate forge direction ───────────────────────────────
            // If neither direction is forgeable, push both items to the
            // next tier as-is so they survive to find valid partners later.
            if (!_forge_engine.is_forgeable(base, sac)) {
                if (_forge_engine.is_forgeable(sac, base)) {
                    std::swap(base, sac);
                } else {
                    next_items.push_back(std::move(base));
                    next_items.push_back(std::move(sac));
                    continue;
                }
            }

            Item saved_base = base;
            Item saved_sac  = sac;

            int32_t cost = _forge_engine.forge_into(base, sac, reg);
            ctx.incr_steps_forged();

            steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

            // Collect into local buffer
            next_items.push_back(std::move(base));
        }

        if (cancelled) break;

        // Collect leftover (if any) into next_items
        if (tiers[tier].size() == 1) {
            next_items.push_back(std::move(tiers[tier][0]));
        }

        // Clear the tier — everything is in next_items now
        tiers[tier].clear();

        // Merge ALL next_items into the next sequential tier (tier+1).
        // This mirrors the old code: every item (forged results AND
        // leftover) goes to the same adjacent tier, guaranteeing they
        // are grouped for pairwise processing in the next iteration.
        size_t nt = tier + 1;
        if (nt >= tiers.size()) tiers.resize(nt + 1);
        for (auto& item : next_items) {
            tiers[nt].push_back(std::move(item));
        }

        // Progress (10 – 95 range)
        auto max_tier = std::max(tiers.size() - 1, size_t{1});
        uint8_t p = 10 + static_cast<uint8_t>(85 * tier / max_tier);
        ctx.report_progress(p < 95 ? p : 95, ProgressStatus::MergingGroups);
    }

    // ── Phase 3: Find final equipment and verify target ─────────────────

    if (!cancelled) {
        for (auto it = tiers.rbegin(); it != tiers.rend(); ++it) {
            for (auto& item : *it) {
                if (item.type == ItemType::Equip
                    && meets_target(item, target))
                {
                    int32_t total_cost = std::accumulate(
                        steps.begin(), steps.end(), int32_t{0},
                        [](int32_t s, const EnchStep& st) { return s + st.cost; });

                    _diag.solution_cost = total_cost;
                    _diag.status = "Complete";
                    ctx.set_exit_diagnostics(_diag);

                    ctx.report_solution(steps);
                    ctx.report_progress(100, ProgressStatus::Complete);
                    return;
                }
            }
        }
    }

    // ── No solution ─────────────────────────────────────────────────────

    _diag.status = cancelled ? "Cancelled" : "CompleteNoSolution";
    ctx.set_exit_diagnostics(_diag);

    ctx.report_progress(100,
        cancelled ? ProgressStatus::Cancelled
                  : ProgressStatus::CompleteNoSolution);
}

// ─── simulate (optimistic) ──────────────────────────────────────────────────

bool HammingAlgorithm::simulate(const AlgorithmInput& input) const noexcept {
    if (input.items.empty()) return false;
    if (meets_target(input.items[0], input.target)) return true;
    return input.items.size() > 1;
}
