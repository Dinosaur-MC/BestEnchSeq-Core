#include "adapters/CompactAdapter.h"
#include "utils/ExpCalculator.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void validate_input(
    const ItemStack& target_item,
    const EnchSet& original_ench,
    const ItemCollection& available_items,
    const EnchantmentRegistry& global_registry)
{
    std::vector<std::string> errors;

    // Check 1: target_item.equipment must be present
    if (!target_item.equipment)
        errors.push_back("target_item.equipment is null");

    // Check 4: target_item.prior_penalty >= 0 && target_item.prior_penalty <= 31
    if (target_item.prior_penalty < 0 || target_item.prior_penalty > 31)
        errors.push_back("target_item.prior_penalty out of range [0, 31]: " + std::to_string(target_item.prior_penalty));

    // Check 5: target_item.durability >= 1 && target_item.durability <= target_item.equipment->max_durability
    if (target_item.equipment) {
        if (target_item.durability < 1 || target_item.durability > target_item.equipment->max_durability)
            errors.push_back("target_item.durability out of range [1, " + std::to_string(target_item.equipment->max_durability) + "]: " + std::to_string(target_item.durability));
    }

    auto check_ench_set = [&](const ::EnchSet& ench_set, const std::string& context) {
        for (const auto& ench : ench_set) {
            // Check 2: valid ID range
            if (ench.id < 0 || ench.id >= static_cast<int32_t>(global_registry.size()))
                errors.push_back(context + ": enchantment id " + std::to_string(ench.id) + " out of range [0, " + std::to_string(global_registry.size()) + ")");

            // Check 3: valid level
            if (ench.id >= 0 && ench.id < static_cast<int32_t>(global_registry.size())) {
                if (ench.level < 1 || ench.level > global_registry.get(ench.id).max_level)
                    errors.push_back(context + ": enchantment level " + std::to_string(ench.level) + " out of range [1, " + std::to_string(global_registry.get(ench.id).max_level) + "] for id " + std::to_string(ench.id));

                // Check 6: equipment applicability
                if (target_item.equipment) {
                    bool applicable = false;
                    for (auto cat_id : global_registry.get(ench.id).applicable_category_ids) {
                        if (cat_id == target_item.equipment->category_id) {
                            applicable = true;
                            break;
                        }
                    }
                    if (!applicable)
                        errors.push_back(context + ": enchantment " + std::to_string(ench.id) + " is not applicable to equipment category " + std::to_string(target_item.equipment->category_id));
                }
            }
        }
    };

    check_ench_set(original_ench, "original_ench");
    check_ench_set(target_item.enchantments, "target_item.enchantments");
    for (size_t i = 0; i < available_items.size(); ++i)
        check_ench_set(available_items[i].enchantments, "available_items[" + std::to_string(i) + "]");

    if (!errors.empty()) {
        std::string msg;
        for (const auto& err : errors)
            msg += err + "; ";
        msg.resize(msg.size() - 2);
        throw std::invalid_argument(std::move(msg));
    }
}

} // anonymous namespace

AlgorithmInput CompactAdapter::apply(
    const ItemStack& target_item,
    const EnchSet& original_ench,
    const ItemCollection& available_items,
    const ForgeConfig& config,
    const EnchantmentRegistry& global_registry)
{
    validate_input(target_item, original_ench, available_items, global_registry);

    // EnchReg pruning: only keep enchantments applicable to target equipment
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

    AlgorithmInput input;
    input.config = config;
    input.equipment = *target_item.equipment;
    input.ench_reg = std::move(ench_reg);

    ItemStack start_equip(*target_item.equipment, original_ench, 0);
    input.items.reserve(1 + available_items.size());
    input.items.push_back(from_domain(start_equip, input.ench_reg));
    for (const auto& book : available_items)
        input.items.push_back(from_domain(book, input.ench_reg));

    input.target.reserve(target_item.enchantments.size());
    for (const auto& e : target_item.enchantments) {
        int16_t local_id = static_cast<int16_t>(input.ench_reg.to_local_id(e.id));
        input.target.push_back({local_id, static_cast<int16_t>(e.level)});
    }

    return input;
}

compact::Item CompactAdapter::from_domain(const ItemStack& item, const compact::EnchReg& reg) {
    compact::Item citem;
    citem.type = item.is_book() ? compact::ItemType::Book : compact::ItemType::Equip;
    citem.ppn = static_cast<int8_t>(item.prior_penalty);
    citem.dur = static_cast<int16_t>(item.durability);
    citem.enchs.reserve(item.enchantments.size());
    for (const auto& ench : item.enchantments) {
        int16_t eid = static_cast<int16_t>(reg.to_local_id(ench.id));
        int16_t elv = static_cast<int16_t>(ench.level);
        citem.enchs.insert({eid, elv});
    }
    return citem;
}

ItemStack CompactAdapter::to_domain(const compact::Item& item, const Equipment& eq,
                                     const compact::EnchReg& reg) {
    ::EnchSet ench_set;
    for (const auto& e : item.enchs) {
        int32_t global_id = reg.get_registry().to_global_id(e.id);
        ench_set.emplace(global_id, e.level);
    }

    if (item.type == compact::ItemType::Book)
        return ItemStack(std::move(ench_set), item.ppn);
    else
        return ItemStack(eq, std::move(ench_set), item.ppn, item.dur);
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

    solutions.reserve(output.steps.size());
    for (const auto& step_list : output.steps) {
        EnchStepList domain_steps;
        domain_steps.reserve(step_list.size());
        for (const auto& s : step_list) {
            auto base = to_domain(s.base, input.equipment, input.ench_reg);
            auto sac  = to_domain(s.sacrifice, input.equipment, input.ench_reg);

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
