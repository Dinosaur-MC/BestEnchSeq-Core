#include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#include "algorithm/ExecutionContext.h"
#include "algorithm/components/SearchUtils.h"
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

using compact::Item;
using compact::ItemType;
using compact::EnchStep;
using compact::EnchReg;

void DiffFirstAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    ctx.report_progress(0.0, ProgressStatus::Starting);

    // ── Quick exits ─────────────────────────────────────────────────────

    if (input.items.empty()) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }
    if (meets_target(input.items[0], target)) {
        ctx.report_compact_solution({});
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        return;
    }
    if (input.items.size() <= 1) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }

    // Copy mutable working set
    std::vector<Item> items(input.items.begin(), input.items.end());
    std::vector<EnchStep> steps;
    steps.reserve(items.size() - 1);

    // Helpers — sort by (ppn ASC, cost DESC for books)
    auto sort_items = [&](std::vector<Item>& v) {
        std::sort(v.begin(), v.end(),
            [&](const Item& a, const Item& b) {
                if (a.ppn != b.ppn) return a.ppn < b.ppn;
                bool a_eq = (a.type == ItemType::Equip);
                bool b_eq = (b.type == ItemType::Equip);
                if (a_eq != b_eq) return a_eq;
                return _forge_engine.estimate_forge_cost(a, a, reg)
                     > _forge_engine.estimate_forge_cost(b, b, reg);
            });
    };

    auto ppn_begin = [&](const std::vector<Item>& v, uint8_t ppn) -> int {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].ppn == ppn) return static_cast<int>(i);
        return -1;
    };
    auto ppn_end = [&](const std::vector<Item>& v, uint8_t ppn) -> int {
        for (size_t i = v.size(); i-- > 0;)
            if (v[i].ppn == ppn) return static_cast<int>(i) + 1;
        return -1;
    };
    auto max_ppn = [&](const std::vector<Item>& v) -> uint8_t {
        uint8_t m = 0;
        for (auto& item : v) if (item.ppn > m) m = item.ppn;
        return m;
    };
    auto min_ppn = [&](const std::vector<Item>& v) -> uint8_t {
        uint8_t m = 255;
        for (auto& item : v) if (item.ppn < m) m = item.ppn;
        return m == 255 ? 0 : m;
    };
    auto find_equip = [&](const std::vector<Item>& v) -> int {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].type == ItemType::Equip) return static_cast<int>(i);
        return -1;
    };

    bool cancelled = false;
    uint8_t cur_ppn = min_ppn(items);
    bool mode1 = false;  // false = PPN-tier mode; true = linear merge

    // ── Main loop ──────────────────────────────────────────────────────

    while (items.size() > 1 && !cancelled) {
        ctx.wait_if_paused();
        if (ctx.is_cancelled()) { cancelled = true; break; }

        sort_items(items);

        if (!mode1) {
            // ── Phase 0: PPN-tier processing ──────────────────────────
            int b = ppn_begin(items, cur_ppn);
            int e = ppn_end(items, cur_ppn);

            if (b < 0 || e - b == 0) {
                // No items at current PPN → advance or switch to mode 1
                if (cur_ppn >= max_ppn(items)) {
                    cur_ppn = min_ppn(items);
                    mode1 = true;
                } else {
                    cur_ppn++;
                }
                continue;
            }

            int eq = find_equip(items);
            if (eq >= b && eq < e) {
                // Equipment is at this PPN tier — forge with cheapest book
                int sac_idx = (eq == b) ? b + 1 : b;  // first book (not equip)
                // Equipment alone at this level (no book) → advance PPN
                if (sac_idx >= e) { cur_ppn++; continue; }

                Item saved_base = items[eq];
                Item saved_sac  = items[sac_idx];
                int32_t cost = _forge_engine.forge_into(items[eq], items[sac_idx], reg);
                ctx.incr_steps_forged();
                steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

                items.erase(items.begin() + sac_idx);
            } else {
                // No equipment at this tier — merge two cheapest books.
                // Single book at this tier: advance PPN so it meets up
                // with items at the next level.
                if (e - b < 2) { cur_ppn++; continue; }

                Item saved_base = items[b];
                Item saved_sac  = items[b + 1];
                int32_t cost = _forge_engine.forge_into(items[b], items[b + 1], reg);
                ctx.incr_steps_forged();
                steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

                items.erase(items.begin() + b + 1);
                // items[b] (forged result) stays — may have higher PPN now
            }
        } else {
            // ── Phase 1: Linear merge ─────────────────────────────────
            int eq = find_equip(items);
            int base_idx = (eq == 1) ? 1 : 0;
            int sac_idx = (eq == 1) ? 0 : 1;

            if (!_forge_engine.is_forgeable(items[base_idx], items[sac_idx])) {
                if (_forge_engine.is_forgeable(items[sac_idx], items[base_idx]))
                    std::swap(base_idx, sac_idx);
                else
                    break;
            }

            Item saved_base = items[base_idx];
            Item saved_sac  = items[sac_idx];
            int32_t cost = _forge_engine.forge_into(items[base_idx], items[sac_idx], reg);
            ctx.incr_steps_forged();
            steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

            items.erase(items.begin() + sac_idx);

            // Check if any PPN tier now has ≥2 items — switch back to mode 0
            uint8_t mp = max_ppn(items);
            for (uint8_t p = 0; p <= mp; ++p) {
                int pb = ppn_begin(items, p);
                int pe = ppn_end(items, p);
                if (pb >= 0 && pe - pb >= 2) {
                    cur_ppn = p;
                    mode1 = false;
                    break;
                }
            }
        }

        double p = 1.0 - static_cast<double>(items.size()) / input.items.size();
        ctx.report_progress(std::min(p, 0.95), ProgressStatus::MergingGroups);
    }

    // ── Check result ──────────────────────────────────────────────────

    if (!cancelled) {
        for (auto& item : items) {
            if (item.type == ItemType::Equip && meets_target(item, target)) {
                int32_t total = std::accumulate(
                    steps.begin(), steps.end(), int32_t{0},
                    [](int32_t s, const EnchStep& st) { return s + st.cost; });

                _diag.solution_cost = total;
                _diag.status = "Complete";
                ctx.report_diagnostics_entries(_diag);

                ctx.report_compact_solution(std::move(steps));
                ctx.report_progress(1.0, ProgressStatus::Complete);
                return;
            }
        }
    }

    _diag.status = cancelled ? "Cancelled" : "CompleteNoSolution";
    ctx.report_diagnostics_entries(_diag);
    ctx.report_progress(1.0,
        cancelled ? ProgressStatus::Cancelled : ProgressStatus::CompleteNoSolution);
}

bool DiffFirstAlgorithm::simulate(const AlgorithmInput& input) const noexcept {
    if (input.items.empty()) return false;
    if (meets_target(input.items[0], input.target)) return true;
    return input.items.size() > 1;
}
