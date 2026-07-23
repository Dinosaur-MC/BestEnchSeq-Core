#include "CompactAdapter.h"
#include "common/utils/ExpCalculator.hpp"

#include <cassert>
#include <unordered_map>
#include <vector>

// ============================================================================
// apply — business → algorithm  (domain → compact)
// ============================================================================

algorithm::AlgorithmInput CompactAdapter::apply(
    const algorithm::Item& target_item,
    const algorithm::EnchSet& source_ench,
    const ::Equipment& target_eq,
    const EnchantmentRegistry& global_registry)
{
    // ── 1. Build algorithm Equipment from business Equipment ───────────
    algorithm::Equipment algo_equip;
    algo_equip.id            = target_eq.name_id.empty() ? 0 : 1; // placeholder
    algo_equip.category_id   = target_eq.category_id;
    algo_equip.max_durability = target_eq.max_durability;

    // ── 2. Determine applicable enchantment IDs ────────────────────────
    const auto& all_infos = global_registry.get_instances();

    std::vector<algorithm::EnchInfo> algo_infos;
    std::vector<int32_t> applicable_global_ids;
    std::unordered_map<int32_t, int16_t> global_to_local;

    for (int32_t gid = 0; gid < static_cast<int32_t>(all_infos.size()); ++gid) {
        const auto& biz = all_infos[gid];
        bool applicable = false;
        for (int32_t cat_id : biz.applicable_category_ids) {
            if (cat_id == algo_equip.category_id || cat_id == 0) {
                applicable = true;
                break;
            }
        }
        if (!applicable) continue;

        algorithm::EnchInfo ai;
        ai.mul     = static_cast<uint16_t>(biz.multiplier);
        ai.mul_b   = static_cast<uint16_t>(biz.multiplier);
        ai.max_lvl = static_cast<uint16_t>(biz.max_level);
        ai.exc_mask.resize(algo_infos.size() / algorithm::MASK_ELEM_SIZE + 1, 0);
        for (size_t local_idx = 0; local_idx < algo_infos.size(); ++local_idx) {
            const std::string& existing_name = all_infos[applicable_global_ids[local_idx]].name_id;
            if (biz.exclusive_set.count(existing_name)) {
                size_t word = local_idx / algorithm::MASK_ELEM_SIZE;
                size_t bit  = local_idx % algorithm::MASK_ELEM_SIZE;
                ai.exc_mask[word] |= (algorithm::MaskType(1) << bit);
                if (word < algo_infos[local_idx].exc_mask.size())
                    algo_infos[local_idx].exc_mask[word] |= (algorithm::MaskType(1) << bit);
            }
        }
        ai.applicable = true;

        int16_t local_id = static_cast<int16_t>(algo_infos.size());
        global_to_local[gid] = local_id;
        applicable_global_ids.push_back(gid);
        algo_infos.push_back(std::move(ai));
    }

    // ── 3. Init compact registry ──────────────────────────────────────
    algorithm::EnchReg ench_reg;
    ench_reg.init(std::move(algo_infos), std::move(applicable_global_ids), algo_equip);

    // ── 4. Remap helper ───────────────────────────────────────────────
    auto remap_ench_set = [&](const algorithm::EnchSet& src) -> algorithm::EnchSet {
        algorithm::EnchSet dst;
        for (const auto& e : src) {
            auto it = global_to_local.find(static_cast<int32_t>(e.id));
            if (it != global_to_local.end())
                dst.insert(algorithm::Ench{it->second, e.level});
        }
        return dst;
    };

    // ── 5. Build AlgorithmInput ───────────────────────────────────────
    algorithm::AlgorithmInput input;
    input.ench_reg = std::move(ench_reg);

    // input.target = desired (wanted enchantments after forge)
    input.target = target_item;
    input.target.enchs = remap_ench_set(input.target.enchs);

    // items[0] = equipment with SOURCE (current) enchantments
    algorithm::Item equip_item = target_item;
    equip_item.enchs = remap_ench_set(source_ench);
    input.items.push_back(std::move(equip_item));

    return input;
}

// ============================================================================
// recall — algorithm → business  (compact → domain)
// ============================================================================

std::vector<Solution> CompactAdapter::recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& input,
    const EnchSet& original_ench,
    const Item& target_item,
    const ItemCollection& available_items)
{
    if (!output.is_valid)
        return {};

    std::vector<Solution> solutions;
    solutions.reserve(output.solutions.size());

    for (const auto& csol : output.solutions) {
        std::vector<Solution::EnchStep> domain_steps;
        domain_steps.reserve(csol.steps.size());

        for (const auto& s : csol.steps) {
            auto base = to_domain(s.base, input.ench_reg);
            auto sac  = to_domain(s.sacrifice, input.ench_reg);

            domain_steps.push_back(Solution::EnchStep{
                std::move(base), std::move(sac),
                s.cost, ExpCalculator::level_to_exp(s.cost)
            });
        }

        // Determine platform from forge config
        MCE plat = MCE::Java;
        if (input.f_config.platform == MCE::Java)
            plat = MCE::Java;
        else if (input.f_config.platform == MCE::Bedrock)
            plat = MCE::Bedrock;
        else
            plat = MCE::Java;

        solutions.push_back(Solution::make(
            plat, original_ench, target_item, available_items,
            domain_steps, true,
            Solution::MetaData{
                output.algorithm_name, output.algorithm_version, 0, 0
            }
        ));
    }

    return solutions;
}

// ============================================================================
// from_domain  —  business Item → algorithm Item
// ============================================================================

algorithm::Item CompactAdapter::from_domain(const Item& item, const algorithm::EnchReg& reg) {
    algorithm::Item citem;
    citem.type = item.is_book() ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
    citem.ppn  = static_cast<uint8_t>(item.prior_penalty);
    citem.dur  = static_cast<int16_t>(item.durability);

    for (const auto& ench : item.enchantments) {
        int16_t eid = static_cast<int16_t>(reg.to_local_id(ench.id));
        if (eid >= 0)
            citem.enchs.insert(algorithm::Ench{eid, static_cast<int16_t>(ench.level)});
    }
    return citem;
}

// ============================================================================
// to_domain  —  algorithm Item → business Item
// ============================================================================

Item CompactAdapter::to_domain(const algorithm::Item& item, const algorithm::EnchReg& reg) {
    EnchSet ench_set;
    for (const auto& e : item.enchs) {
        int32_t global_id = reg.to_global_id(e.id);
        if (global_id >= 0)
            ench_set.emplace(global_id, e.level);
    }

    if (item.type == algorithm::ItemType::Book)
        return Item(ench_set, item.ppn);
    else
        return Item(
            ::Equipment{"", "", reg.get_target_equip().category_id,
                         reg.get_target_equip().max_durability},
            ench_set, item.ppn, item.dur);
}
