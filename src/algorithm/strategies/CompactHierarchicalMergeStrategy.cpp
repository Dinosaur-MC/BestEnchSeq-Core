#include "CompactHierarchicalMergeStrategy.h"
#include "utils/CompactAdapter.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

int32_t CompactHierarchicalMergeStrategy::effective_multiplier(
    const compact::Item& item, const compact::EnchReg& reg)
{
    int32_t max_mult = 1;
    for (const auto& e : item.enchs) {
        int32_t m = compact::book_multiplier(reg.get_multiplier(e.id));
        if (m > max_mult) max_mult = m;
    }
    return max_mult;
}

compact::Item CompactHierarchicalMergeStrategy::merge_group(
    std::vector<compact::Item>& group,
    std::vector<compact::EnchStep>& steps,
    const compact::EnchReg& reg,
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
                if (!compact::CompactForgeEngine::is_forgeable(group[i], group[j]) &&
                    !compact::CompactForgeEngine::is_forgeable(group[j], group[i]))
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
        if (compact::CompactForgeEngine::is_forgeable(group[best_i], group[best_j])) {
            base_idx = best_i; sac_idx = best_j;
        } else {
            base_idx = best_j; sac_idx = best_i;
        }

        compact::Item saved_base = group[base_idx];
        compact::Item saved_sac  = group[sac_idx];

        int32_t cost = _forge_engine.forge_into(group[base_idx], group[sac_idx], reg);
        steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

        group.erase(group.begin() + sac_idx);
    }

    return group[0];
}

void CompactHierarchicalMergeStrategy::execute(
    const AlgorithmInput& input, ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    //── Boundary: prepare compact data ────────────────────────────────────
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(EnchantmentRegistry::get_instance(), *input.target_item.equipment);
    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;

    if (items.size() <= 1) {
        ctx.report_solution_found({});
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        return;
    }

    // items[0] = equipment, items[1..] = books
    auto equip = std::move(items[0]);
    std::vector<compact::Item> books;
    books.reserve(items.size() - 1);
    for (size_t k = 1; k < items.size(); ++k)
        books.push_back(std::move(items[k]));

    std::vector<compact::EnchStep> compact_steps;

    // Phase 1: Optional dedup — merge same-enchantment books (>7 items)
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
                if (!compact::CompactForgeEngine::is_forgeable(base, books[sac_idx]))
                    continue;

                compact::Item saved_base = base;
                compact::Item saved_sac  = books[sac_idx];

                int32_t cost = _forge_engine.forge_into(base, books[sac_idx], ench_reg);
                compact_steps.push_back({std::move(saved_base), std::move(saved_sac), cost});

                if (sac_idx < books.size())
                    books.erase(books.begin() + sac_idx);
            }
        }
    }

    // Phase 2: Group by effective multiplier tier
    std::vector<compact::Item> low_group, mid_group, high_group;

    for (auto& book : books) {
        int32_t mult = effective_multiplier(book, ench_reg);
        if (mult <= 1)
            low_group.push_back(std::move(book));
        else if (mult <= 2)
            mid_group.push_back(std::move(book));
        else
            high_group.push_back(std::move(book));
    }

    ctx.report_progress(0.3, ProgressStatus::MergingWithinGroups);

    auto low_merged  = merge_group(low_group, compact_steps, ench_reg, ctx);
    auto mid_merged  = merge_group(mid_group, compact_steps, ench_reg, ctx);
    auto high_merged = merge_group(high_group, compact_steps, ench_reg, ctx);

    // Collect non-empty results, ordered by multiplier
    struct GroupResult { compact::Item book; int32_t mult; };
    std::vector<GroupResult> group_results;
    if (!low_merged.enchs.empty())
        group_results.push_back({std::move(low_merged), 1});
    if (!mid_merged.enchs.empty())
        group_results.push_back({std::move(mid_merged), 2});
    if (!high_merged.enchs.empty())
        group_results.push_back({std::move(high_merged), 3});

    ctx.report_progress(0.6, ProgressStatus::ApplyingToEquipment);

    if (group_results.empty()) {
        auto steps = compact::to_domain(compact_steps.begin(), compact_steps.end(), ci.equipment);
        ctx.report_solution_found(steps);
        ctx.report_progress(1.0, ProgressStatus::Complete);
        return;
    }

    // Phase 3: Merge group books together, then apply to equipment
    compact::Item combined = group_results[0].book;
    for (size_t g = 1; g < group_results.size(); ++g) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& next_book = group_results[g].book;
        if (!compact::CompactForgeEngine::is_forgeable(combined, next_book))
            continue;

        compact::Item saved_base = combined;
        int32_t cost = _forge_engine.forge_into(combined, next_book, ench_reg);
        compact_steps.push_back({std::move(saved_base), std::move(next_book), cost});
    }

    // Apply combined book to equipment
    if (compact::CompactForgeEngine::is_forgeable(equip, combined)) {
        compact::Item saved_equip = equip;
        int32_t cost = _forge_engine.forge_into(equip, combined, ench_reg);
        compact_steps.push_back({std::move(saved_equip), std::move(combined), cost});
    }

    //── Boundary: convert to domain for output ───────────────────────────
    auto steps = compact::to_domain(compact_steps.begin(), compact_steps.end(), ci.equipment);
    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
