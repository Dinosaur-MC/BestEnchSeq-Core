#include "HammingAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>
namespace algorithm {

using namespace algorithm;

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
    const EnchReg& reg,
    bool preserve_equip_order) const
{
    const size_t n = items.size();
    if (n <= 2) return;

    // Sort by forge cost descending (expensive books first → merge early).
    // Equipment unconditionally goes to position 0 — the root of the
    // balanced merge tree (position k merges ~popcount(k) times).
    // Inventory mode (preserve_equip_order): 装备保持 resolver 进入顺序——
    // 位置 0 恒为 resolver 选定的无冲突 base（平衡树根），不按自锻成本重排。
    // 否则冲突装备（保留的牺牲品）可能抢占根位置，把需要的书全锻入它而浪费
    // （forge 遇冲突丢弃魔咒），导致假"目标不可达"。
    std::stable_sort(items.begin(), items.end(),
        [&](const Item& a, const Item& b) {
            bool a_eq = (a.type == ItemType::Equip);
            bool b_eq = (b.type == ItemType::Equip);
            if (a_eq != b_eq) return a_eq;
            if (preserve_equip_order && a_eq)
                return false;  // 装备保持进入顺序（stable_sort 下等价即保序）
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

void HammingAlgorithm::execute(const AlgorithmInput &input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config.forge);
    _ench_reg = &input.registry;
    const auto& reg = input.registry;
    const auto& target = input.target;
    ctx.report_progress(0, ProgressStatus::Starting);

    // Working item set comes from the algorithm's resolver.
    auto items = get_resolver()->resolve(input);
    normalize_base_equipment(items);

    // ── Quick exit checks ──────────────────────────────────────────────

    if (items.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    if (meets_target(items[0], target)) {
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        return;
    }

    if (items.size() <= 1) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    // ── Phase 1: Seed the PPN-tiered work queue ──────────────────────────

    const size_t initial_item_count = items.size();
    int max_ppn = 0;
    for (const auto& item : items)
        if (item.ppn > max_ppn) max_ppn = item.ppn;

    std::vector<std::vector<Item>> tiers(static_cast<size_t>(max_ppn) + 1);
    for (auto& item : items)
        tiers[item.ppn].push_back(std::move(item));

    // 终止边界：浪费性配对（目标魔咒与冲突 base）通过保送逐层上浮，最迟在
    // base 所在层（max_ppn）解析；若池中无兼容 base（如目标魔咒互相冲突），
    // 保送将无限继续。此上界保证终止——超界即真正不可达，进入最终扫描得无解。
    const size_t max_tiers = static_cast<size_t>(max_ppn) + initial_item_count + 2;

    std::vector<EnchStep> steps;
    steps.reserve(items.size() - 1);

    bool cancelled = false;

    // ── Phase 2: Bottom-up sequential-tier processing (the Hamming triangle) ──

    // Items bubble up one tier per outer iteration (tier → tier+1).
    // Forged results and leftovers always go to tier+1, guaranteeing every
    // item converges at the final tier.
    for (size_t tier = 0; tier < tiers.size() && tier <= max_tiers && !cancelled; ++tier) {
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

        // Arrange items in this tier using popcount ordering.
        // Inventory 模式保留 resolver 装备顺序（位置 0 = resolver base）；
        // direct 模式单装备，保持原逻辑。
        arrange_by_popcount(tiers[tier], reg, input.is_inventory());

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

            // ── Waste-avoidance（相邻位 swap / carry）───────────────────
            // 若该配对会把一个仍需要的目标魔咒浪费掉（sac 携带目标魔咒、base 持
            // 冲突魔咒致其被 forge_into 丢弃），优先尝试书-书反向配对——把原 base
            // 锻入原 sac（如 direct 模式书目标：源书与所需目标书冲突时，把源书锻
            // 入目标书只会丢弃冲突魔咒而非目标魔咒）。设备配对保留 resolver 的
            // base 选择，退回相邻位 swap / carry：将冲突物品（base）与汉明排列
            // 序列中的相邻下一位交换（位置 1 ↔ 位置 2，popcount 同层，保持平衡
            // 树）：
            //   ① 交换后 base 与目标书配对不浪费 → 正常锻造（冲突物品与后续项合并）
            //   ② 交换后仍浪费 → 双双保送下一 tier，可继续 swap 直到冲突被消耗
            // 序列耗尽无相邻位 → 双双保送。目标书因此存活到兼容的 base，避免假
            // "目标不可达"。（ForgeEngine 行为是 MC 原版机制，不改。）
            if (merge_wastes_target(base, sac, target, reg)) {
                // Book-book pair with no fixed root: try the reverse
                // orientation — forge the original base INTO the sacrifice if
                // that direction is not wasteful (e.g. a direct-mode book target
                // whose source book conflicts with the needed target book:
                // forging the source into the target book drops the conflict
                // instead of the target enchant).  Equipment pairs keep the
                // resolver's base choice (the adjacent-position swap below).
                if (base.type == ItemType::Book && sac.type == ItemType::Book &&
                    !merge_wastes_target(sac, base, target, reg)) {
                    std::swap(base, sac);
                } else if (!tiers[tier].empty()) {
                    Item next = std::move(tiers[tier].front());
                    tiers[tier].erase(tiers[tier].begin());
                    // base（冲突物品）放回序列（与后续项配对）；目标书与 next 配对。
                    tiers[tier].insert(tiers[tier].begin(), std::move(base));
                    base = std::move(next);
                    if (merge_wastes_target(base, sac, target, reg)) {
                        // 交换后仍浪费 → 双双保送（下一 tier 可继续 swap）。
                        next_items.push_back(std::move(base));
                        next_items.push_back(std::move(sac));
                        continue;
                    }
                } else {
                    // 无相邻位 → 双双保送。
                    next_items.push_back(std::move(base));
                    next_items.push_back(std::move(sac));
                    continue;
                }
            }

            Item saved_base = base;
            Item saved_sac  = sac;

            int32_t cost = _forge_engine.forge_into(base, sac, reg);
            ctx.incr_steps_forged();

            steps.push_back({std::move(saved_base), std::move(saved_sac), base, cost});

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
                // A 0-step result in the final scan is always spurious — the
                // only legitimate "already met" case (source ≥ target) was
                // short-circuited above; an un-forged pool item (e.g. a
                // resolver-generated gap book) that meets the target here must
                // not be reported as a 0-step solution.
                if (meets_target(item, target) && !steps.empty())
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

// ─── evaluate ──────────────────────────────────────────────────────────────────

double HammingAlgorithm::evaluate(int16_t ench_count) const noexcept {
    (void)ench_count;
    // O(n log n) deterministic construction — always sub-millisecond.
    return 0;
}


} // namespace algorithm
