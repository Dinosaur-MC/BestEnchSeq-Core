#include "HierarchicalMergeAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

int32_t HierarchicalMergeAlgorithm::effective_multiplier(
    const Item& item, const EnchReg& reg) const
{
    int32_t max_mult = 1;
    for (const auto& e : item.enchs) {
        int32_t m = reg[e.id].mul_b;
        if (m > max_mult) max_mult = m;
    }
    return max_mult;
}

Item HierarchicalMergeAlgorithm::merge_group(
    std::vector<Item>& group,
    std::vector<EnchStep>& steps,
    const EnchReg& reg,
    ExecutionContext& ctx,
    const std::chrono::steady_clock::time_point& start,
    const SearchConfig& search)
{
    if (group.empty()) return {};
    if (group.size() == 1) return group[0];

    while (group.size() > 1) {
        if (ctx.is_cancelled()) return group[0];
        ctx.wait_if_paused();

        size_t best_i = 0, best_j = 1;
        int32_t best_diff = std::abs(static_cast<int32_t>(group[0].ppn)
                                   - static_cast<int32_t>(group[1].ppn));

        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                if (!_forge_engine.is_forgeable(group[i], group[j]) &&
                    !_forge_engine.is_forgeable(group[j], group[i]))
                    continue;

                int32_t diff = std::abs(static_cast<int32_t>(group[i].ppn)
                                      - static_cast<int32_t>(group[j].ppn));
                if (diff < best_diff) {
                    best_diff = diff;
                    best_i = i; best_j = j;
                }
            }
        }

        size_t base_idx, sac_idx;
        if (_forge_engine.is_forgeable(group[best_i], group[best_j])) {
            base_idx = best_i; sac_idx = best_j;
        } else {
            base_idx = best_j; sac_idx = best_i;
        }

        Item saved_base = group[base_idx];
        Item saved_sac  = group[sac_idx];

        int32_t cost = _forge_engine.forge_into(group[base_idx], group[sac_idx], reg);
        ctx.incr_steps_forged();
        steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

        {
            if (search.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > search.max_search_time) break;
            }
        }

        group.erase(group.begin() + sac_idx);
    }

    return group[0];
}

void HierarchicalMergeAlgorithm::execute(
    const AlgorithmInput& input, ExecutionContext& ctx)
{
    _forge_engine.set_config(input.f_config);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    const auto& search = input.s_config;
    ctx.report_progress(0, ProgressStatus::Starting);

    auto start = std::chrono::steady_clock::now();
    int32_t steps_performed = 0;

    if (items.size() <= 1) {
        _diag.status = "GoalAlreadyMet";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        return;
    }

    auto equip = items[0];
    std::vector<Item> books;
    books.reserve(items.size() - 1);
    for (size_t k = 1; k < items.size(); ++k)
        books.push_back(items[k]);

    std::vector<EnchStep> compact_steps;

    // Phase 1: Dedup same-enchantment books via proper pairwise merging.
    // Sequential merge into one accumulator (as previously implemented) fails
    // for same-level books: 3→(3+3=4)→(4+3=4)→... never reaches level 5.
    // Pairwise: same-enchant + same-level books are paired, producing the
    // next level, then re-paired in subsequent passes.
    if (books.size() > kDedupThreshold) {
        for (int pass = 0; pass < 4 && books.size() > 1; ++pass) {
            // Sort by (enchant_id, level) so that same-enchant/same-level
            // books are adjacent for pairing.
            std::sort(books.begin(), books.end(),
                [](const Item& a, const Item& b) {
                    if (a.enchs.size() != 1 || b.enchs.size() != 1)
                        return a.enchs.size() < b.enchs.size();
                    auto ae = *a.enchs.begin();
                    auto be = *b.enchs.begin();
                    return ae.id < be.id || (ae.id == be.id && ae.level < be.level);
                });

            std::vector<Item> next;
            next.reserve(books.size());
            for (size_t i = 0; i < books.size(); ++i) {
                if (books[i].enchs.size() != 1) {
                    next.push_back(std::move(books[i]));
                    continue;
                }
                auto eid = books[i].enchs.begin()->id;
                auto lvl = books[i].enchs.begin()->level;

                // Look ahead for another book with same enchant AND same level
                if (i + 1 < books.size() && books[i + 1].enchs.size() == 1) {
                    auto ne = *books[i + 1].enchs.begin();
                    if (ne.id == eid && ne.level == lvl
                        && _forge_engine.is_forgeable(books[i], books[i + 1]))
                    {
                        Item saved = books[i];
                        int32_t cost = _forge_engine.forge_into(books[i], books[i + 1], reg);
                        ctx.incr_steps_forged();
                        compact_steps.push_back({std::move(saved), std::move(books[i + 1]), cost});
                        ++steps_performed;
                        next.push_back(std::move(books[i]));  // merged result
                        ++i;  // skip the paired sacrifice
                        continue;
                    }
                }
                next.push_back(std::move(books[i]));
            }
            books = std::move(next);

            {
                auto cfg = search;
                if (cfg.max_search_time.count() > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed > cfg.max_search_time) goto phase2;
                }
                if (cfg.max_solutions > 0 && steps_performed >= cfg.max_solutions) goto phase2;
            }
        }
    }

phase2:

    // Phase 2: Group by effective multiplier tier
    std::vector<Item> low_group, mid_group, high_group;
    for (auto& book : books) {
        int32_t mult = effective_multiplier(book, reg);
        if (mult <= 1)
            low_group.push_back(std::move(book));
        else if (mult <= 2)
            mid_group.push_back(std::move(book));
        else
            high_group.push_back(std::move(book));
    }

    ctx.report_progress(30, ProgressStatus::MergingWithinGroups);

    auto low_merged  = merge_group(low_group, compact_steps, reg, ctx, start, search);
    auto mid_merged  = merge_group(mid_group, compact_steps, reg, ctx, start, search);
    auto high_merged = merge_group(high_group, compact_steps, reg, ctx, start, search);

    struct GroupResult { Item book; int32_t mult; };
    std::vector<GroupResult> group_results;
    if (!low_merged.enchs.empty())
        group_results.push_back({std::move(low_merged), 1});
    if (!mid_merged.enchs.empty())
        group_results.push_back({std::move(mid_merged), 2});
    if (!high_merged.enchs.empty())
        group_results.push_back({std::move(high_merged), 3});

    ctx.report_progress(60, ProgressStatus::ApplyingToEquipment);

    if (group_results.empty()) {
        _diag.status = "Complete";
        ctx.set_exit_diagnostics(_diag);

        ctx.report_solution(compact_steps);
        ctx.report_progress(100, ProgressStatus::Complete);
        return;
    }

    // Phase 3: Merge groups together, then apply to equipment
    Item combined = group_results[0].book;
    for (size_t g = 1; g < group_results.size(); ++g) {
        if (ctx.is_cancelled()) {
            _diag.status = "Cancelled";
            ctx.set_exit_diagnostics(_diag);
            return;
        }
        ctx.wait_if_paused();

        auto& next_book = group_results[g].book;
        if (!_forge_engine.is_forgeable(combined, next_book))
            continue;

        Item saved_base = combined;
        int32_t cost = _forge_engine.forge_into(combined, next_book, reg);
        ctx.incr_steps_forged();
        compact_steps.push_back({std::move(saved_base), std::move(next_book), cost});

        ++steps_performed;

        {
            auto cfg = search;
            if (cfg.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > cfg.max_search_time) break;
            }
            if (cfg.max_solutions > 0 && steps_performed >= cfg.max_solutions) break;
        }
    }

    if (_forge_engine.is_forgeable(equip, combined)) {
        Item saved_equip = equip;
        int32_t cost = _forge_engine.forge_into(equip, combined, reg);
        ctx.incr_steps_forged();
        compact_steps.push_back({std::move(saved_equip), std::move(combined), cost});

        ++steps_performed;
    }

    // Verify final equipment achieves the target BEFORE setting diagnostics
    {
        bool ok = true;
        for (const auto& t : target.enchs) {
            auto it = equip.enchs.find(t.id);
            if (it == equip.enchs.end() || it->level < t.level) { ok = false; break; }
        }
        if (!ok) {
            _diag.status = "CompleteNoSolution";
            ctx.set_exit_diagnostics(_diag);
            ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
            return;
        }
    }

    _diag.status = "Complete";
    ctx.set_exit_diagnostics(_diag);

    ctx.report_solution(compact_steps);
    ctx.report_progress(100, ProgressStatus::Complete);
}

} // namespace algorithm
