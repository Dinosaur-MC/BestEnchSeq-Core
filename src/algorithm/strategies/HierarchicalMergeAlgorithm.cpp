#include "HierarchicalMergeAlgorithm.h"
#include "../ExecutionContext.h"
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

int32_t HierarchicalMergeAlgorithm::effective_multiplier(
    const Item& item, const EnchReg& reg) const
{
    int32_t max_mult = 1;
    for (const auto& e : item.enchs) {
        int32_t m = _forge_engine.book_multiplier(reg.get_multiplier(e.id));
        if (m > max_mult) max_mult = m;
    }
    return max_mult;
}

Item HierarchicalMergeAlgorithm::merge_group(
    std::vector<Item>& group,
    std::vector<EnchStep>& steps,
    const EnchReg& reg,
    ExecutionContext& ctx)
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

        group.erase(group.begin() + sac_idx);
    }

    return group[0];
}

void HierarchicalMergeAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    if (items.size() <= 1) {
        _diag.label = "hierarchical";
        _diag.status = "GoalAlreadyMet";
        _diag.write();
        ctx.report_compact_solution({});
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        return;
    }

    auto equip = items[0];
    std::vector<Item> books;
    books.reserve(items.size() - 1);
    for (size_t k = 1; k < items.size(); ++k)
        books.push_back(items[k]);

    std::vector<EnchStep> compact_steps;

    // Phase 1: Dedup same-enchantment books (when >7 books)
    if (books.size() > 7) {
        std::unordered_map<int32_t, std::vector<size_t>> ench_to_books;
        for (size_t i = 0; i < books.size(); ++i) {
            if (books[i].enchs.size() == 1) {
                int32_t eid = books[i].enchs.begin()->id;
                ench_to_books[eid].push_back(i);
            }
        }

        for (auto& [eid, indices] : ench_to_books) {
            if (indices.size() < 2) continue;
            std::sort(indices.begin(), indices.end(), std::greater<>{});

            auto& base = books[indices.back()];
            for (size_t idx_idx = 1; idx_idx < indices.size(); ++idx_idx) {
                size_t sac_idx = indices[idx_idx];
                if (sac_idx >= books.size()) continue;
                if (sac_idx == indices.back()) continue; // self-forge guard — don't forge base into itself
                if (!_forge_engine.is_forgeable(base, books[sac_idx]))
                    continue;

                Item saved_base = base;
                Item saved_sac  = books[sac_idx];

                int32_t cost = _forge_engine.forge_into(base, books[sac_idx], reg);
                ctx.incr_steps_forged();
                compact_steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

                if (sac_idx < books.size())
                    books.erase(books.begin() + sac_idx);
            }
        }
    }

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

    ctx.report_progress(0.3, ProgressStatus::MergingWithinGroups);

    auto low_merged  = merge_group(low_group, compact_steps, reg, ctx);
    auto mid_merged  = merge_group(mid_group, compact_steps, reg, ctx);
    auto high_merged = merge_group(high_group, compact_steps, reg, ctx);

    struct GroupResult { Item book; int32_t mult; };
    std::vector<GroupResult> group_results;
    if (!low_merged.enchs.empty())
        group_results.push_back({std::move(low_merged), 1});
    if (!mid_merged.enchs.empty())
        group_results.push_back({std::move(mid_merged), 2});
    if (!high_merged.enchs.empty())
        group_results.push_back({std::move(high_merged), 3});

    ctx.report_progress(0.6, ProgressStatus::ApplyingToEquipment);

    if (group_results.empty()) {
        _diag.label = "hierarchical";
        _diag.steps_forged = compact_steps.size();
        _diag.status = "Complete";
        _diag.write();

        ctx.report_compact_solution(std::move(compact_steps));
        ctx.report_progress(1.0, ProgressStatus::Complete);
        return;
    }

    // Phase 3: Merge groups together, then apply to equipment
    Item combined = group_results[0].book;
    for (size_t g = 1; g < group_results.size(); ++g) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& next_book = group_results[g].book;
        if (!_forge_engine.is_forgeable(combined, next_book))
            continue;

        Item saved_base = combined;
        int32_t cost = _forge_engine.forge_into(combined, next_book, reg);
        ctx.incr_steps_forged();
        compact_steps.push_back({std::move(saved_base), std::move(next_book), cost});
    }

    if (_forge_engine.is_forgeable(equip, combined)) {
        Item saved_equip = equip;
        int32_t cost = _forge_engine.forge_into(equip, combined, reg);
        ctx.incr_steps_forged();
        compact_steps.push_back({std::move(saved_equip), std::move(combined), cost});
    }

    _diag.label = "hierarchical";
    _diag.steps_forged = compact_steps.size();
    _diag.status = "Complete";
    _diag.write();

    ctx.report_compact_solution(std::move(compact_steps));
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
