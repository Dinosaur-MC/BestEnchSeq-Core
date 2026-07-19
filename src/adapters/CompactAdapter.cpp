#include "adapters/CompactAdapter.h"
#include "utils/ExpCalculator.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

AlgorithmInput CompactAdapter::apply(
    const ResolvedInput& resolved,
    const EnchantmentRegistry& global_registry)
{
    const auto& target_item = resolved.target_item;
    const auto& original_ench = resolved.source_ench;
    const auto& target_ench = resolved.target_ench;
    const auto& available_items = resolved.available_items;

    // ── Strict validation (compact-level) ──────────────────────────
    if (!target_item.equipment)
        throw std::invalid_argument("target_item.equipment is null");
    if (target_item.prior_penalty < 0 || target_item.prior_penalty > 31)
        throw std::invalid_argument("target_item.prior_penalty out of range [0, 31]: " +
            std::to_string(target_item.prior_penalty));
    if (target_item.equipment) {
        if (target_item.durability < 1 ||
            target_item.durability > target_item.equipment->max_durability)
            throw std::invalid_argument("target_item.durability out of range [1, " +
                std::to_string(target_item.equipment->max_durability) + "]: " +
                std::to_string(target_item.durability));
    }

    // Validate compact-level ID ranges for all items
    auto check_compact_ids = [&](const ::EnchSet& ench_set, const std::string& context) {
        for (const auto& ench : ench_set) {
            if (ench.id < 0 || ench.id >= static_cast<int32_t>(global_registry.size()))
                throw std::invalid_argument(context + ": id " + std::to_string(ench.id) + " out of range");
            if (ench.level < 1 || ench.level > global_registry.get(ench.id).max_level)
                throw std::invalid_argument(context + ": level " + std::to_string(ench.level) + " out of range");
        }
    };
    check_compact_ids(original_ench, "source_ench");
    check_compact_ids(target_ench, "target_ench");
    for (size_t i = 0; i < available_items.size(); ++i)
        check_compact_ids(available_items[i].enchantments, "books[" + std::to_string(i) + "]");

    // ── EnchReg pruning ────────────────────────────────────────────
    std::vector<int32_t> applicable_ids;
    for (const auto& info : global_registry.get_instances()) {
        for (auto cat_id : info.applicable_category_ids) {
            if (cat_id == target_item.equipment->category_id) {
                applicable_ids.push_back(global_registry.get_id(info.name_id));
                break;
            }
        }
    }
    auto subset = global_registry.create_subset(applicable_ids);

    compact::EnchReg ench_reg;
    ench_reg.init(subset, *target_item.equipment);

    // ── Domain to compact conversion ────────────────────────────────
    AlgorithmInput input;
    input.ench_reg = std::move(ench_reg);  // must be set before from_domain calls

    ItemStack start_equip(*target_item.equipment, original_ench, 0);
    input.items.reserve(1 + available_items.size());
    input.items.push_back(from_domain(start_equip, input.ench_reg));
    for (const auto& book : available_items)
        input.items.push_back(from_domain(book, input.ench_reg));

    input.target.reserve(target_ench.size());
    for (const auto& e : target_ench) {
        int16_t local_id = static_cast<int16_t>(input.ench_reg.to_local_id(e.id));
        if (local_id < 0) continue;
        input.target.push_back({local_id, static_cast<int16_t>(e.level)});
    }

    return input;
}

compact::Item CompactAdapter::from_domain(const ItemStack& item, const compact::EnchReg& reg) {
    compact::Item citem;
    citem.type = item.is_book() ? compact::ItemType::Book : compact::ItemType::Equip;
    citem.ppn = [&]() {
        assert(item.prior_penalty >= 0 && item.prior_penalty <= 31);
        return static_cast<int8_t>(item.prior_penalty);
    }();
    citem.dur = static_cast<int16_t>(item.durability);
    citem.enchs.reserve(item.enchantments.size());
    for (const auto& ench : item.enchantments) {
        int16_t eid = static_cast<int16_t>(reg.to_local_id(ench.id));
        int16_t elv = static_cast<int16_t>(ench.level);
        citem.enchs.insert({eid, elv});
    }
    return citem;
}

ItemStack CompactAdapter::to_domain(const compact::Item& item, const compact::EnchReg& reg) {
    ::EnchSet ench_set;
    for (const auto& e : item.enchs) {
        int32_t global_id = reg.get_registry().to_global_id(e.id);
        ench_set.emplace(global_id, e.level);
    }

    if (item.type == compact::ItemType::Book)
        return ItemStack(std::move(ench_set), item.ppn);
    else
        return ItemStack(reg.get_target_equip(), std::move(ench_set), item.ppn, item.dur);
}

std::vector<EnchSolution> CompactAdapter::recall(
    const AlgorithmOutput& output,
    const AlgorithmInput& input,
    const EnchSet& original_ench,
    const ItemStack& target_item,
    const ItemCollection& available_items)
{
    std::vector<EnchSolution> solutions;
    if (!output.is_valid) return solutions;

    solutions.reserve(output.solutions.size());
    for (const auto& csol : output.solutions) {
        EnchStepList domain_steps;
        domain_steps.reserve(csol.steps.size());
        for (const auto& s : csol.steps) {
            auto base = to_domain(s.base, input.ench_reg);
            auto sac  = to_domain(s.sacrifice, input.ench_reg);

            domain_steps.push_back(EnchSolution::EnchStep{
                std::move(base), std::move(sac), s.cost,
                ExpCalculator::level_to_exp(s.cost)
            });
        }

        solutions.push_back(EnchSolution::make(
            input.config.platform, original_ench, target_item, available_items,
            domain_steps, true,
            EnchSolution::MetaData{
                output.algorithm_name, output.algorithm_version, 0, 0
            }
        ));
    }
    return solutions;
}
