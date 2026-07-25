#include "CompactAdapter.h"
#include "common/CommonTypes.h"
#include "common/utils/ExpCalculator.hpp"

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

// ============================================================================
// apply — Profile + SolveRequest -> AlgorithmInput
// ============================================================================

algorithm::AlgorithmInput CompactAdapter::apply(
    const Profile& profile,
    const SolveRequest& request)
{
    // ── 1. Resolve registries and equipment ─────────────────────────────
    const auto& ench_registry = profile.ench();
    const auto& eq_registry   = profile.eq();

    // Look up target equipment by item id.  Books may not be in the
    // equipment registry — fall back to a minimal placeholder.
    const bool is_book = request.target_item.is_book();
    ::Equipment target_eq;
    if (is_book) {
        target_eq = ::Equipment{request.target_item.id,
                                request.target_item.id.str(), NSID(), 0};
    } else {
        try {
            target_eq = eq_registry.at(request.target_item.id);
        } catch (const std::out_of_range&) {
            target_eq = ::Equipment{request.target_item.id,
                                    request.target_item.id.str(), NSID(), 0};
        }
    }

    // ── 2. Build algorithm Equipment ────────────────────────────────────
    algorithm::Equipment algo_equip;
    algo_equip.id             = 0; // placeholder; TODO: map NSID to int32_t
    algo_equip.category_id    = 0;
    algo_equip.max_durability = target_eq.max_durability;

    // ── 3. Sort full registry by NSID (deterministic global_id) ─────────
    const auto& all_infos_map = ench_registry.data();
    std::vector<std::pair<NSID, EnchInfo>> sorted_infos;
    sorted_infos.reserve(all_infos_map.size());
    for (const auto& [nsid, info] : all_infos_map)
        sorted_infos.emplace_back(nsid, info);
    std::sort(sorted_infos.begin(), sorted_infos.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // ── 4. Filter applicable enchantments, build compact EnchReg ───────
    std::vector<algorithm::EnchInfo> algo_infos;
    std::vector<NSID> applicable_global_nsids;
    std::unordered_map<NSID, int16_t> nsid_to_local;

    for (int32_t gid = 0; gid < static_cast<int32_t>(sorted_infos.size()); ++gid) {
        const auto& biz = sorted_infos[gid].second;
        bool applicable = biz.applicable_equipments.count(target_eq.category) > 0
                       || biz.applicable_equipments.count(NSID("#minecraft:any")) > 0;
        if (!applicable) continue;

        algorithm::EnchInfo ai;
        ai.mul     = static_cast<uint16_t>(biz.multiplier);
        ai.mul_b   = static_cast<uint16_t>(biz.multiplier);
        ai.max_lvl = static_cast<uint16_t>(biz.max_level);
        ai.exc_mask.resize(algo_infos.size() / algorithm::MASK_ELEM_SIZE + 1, 0);
        for (size_t local_idx = 0; local_idx < algo_infos.size(); ++local_idx) {
            const NSID& existing_nsid = applicable_global_nsids[local_idx];
            if (biz.exclusive_set.count(existing_nsid)) {
                size_t word = local_idx / algorithm::MASK_ELEM_SIZE;
                size_t bit  = local_idx % algorithm::MASK_ELEM_SIZE;
                ai.exc_mask[word] |= (algorithm::MaskType(1) << bit);
                if (word < algo_infos[local_idx].exc_mask.size())
                    algo_infos[local_idx].exc_mask[word] |= (algorithm::MaskType(1) << bit);
            }
        }
        ai.applicable = true;

        int16_t local_id = static_cast<int16_t>(algo_infos.size());
        nsid_to_local[sorted_infos[gid].first] = local_id;
        applicable_global_nsids.push_back(sorted_infos[gid].first);
        algo_infos.push_back(std::move(ai));
    }

    // ── 5. Init compact registry ───────────────────────────────────────
    algorithm::EnchReg ench_reg;
    ench_reg.init(std::move(algo_infos),
                  std::move(applicable_global_nsids), algo_equip);

    // ── 6. NSID -> local_id remap helper ───────────────────────────────
    auto remap_nsid_to_local = [&](const EnchSet& src) -> algorithm::EnchSet {
        algorithm::EnchSet dst;
        for (const auto& e : src) {
            auto it = nsid_to_local.find(e.id);
            if (it != nsid_to_local.end())
                dst.insert(algorithm::Ench{it->second,
                            static_cast<int16_t>(e.level)});
        }
        return dst;
    };

    // ── 7. Build AlgorithmInput skeleton ───────────────────────────────
    algorithm::AlgorithmInput input;
    input.ench_reg = std::move(ench_reg);
    input.f_config = request.forge_config;
    input.s_config = request.search_config;
    input.mode     = request.mode;

    // Convert target_item (domain -> algorithm)
    algorithm::Item algo_target;
    algo_target.type = is_book ? algorithm::ItemType::Book
                               : algorithm::ItemType::Equip;
    algo_target.ppn  = static_cast<uint8_t>(request.target_item.prior_penalty);
    algo_target.dur  = static_cast<int16_t>(request.target_item.durability);
    algo_target.enchs = remap_nsid_to_local(request.target_item.enchantments);
    input.target = std::move(algo_target);

    // ── 8. Convert payload ─────────────────────────────────────────────
    std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, DirectPayload>) {
            // Direct mode: items[0] = equipment with CURRENT (source) enchants
            algorithm::Item equip_item = input.target;
            equip_item.enchs = remap_nsid_to_local(payload.source_enchantments);
            input.items.push_back(std::move(equip_item));

            // Data union: source enchantments as EnchCollection
            algorithm::EnchCollection src_vec;
            for (const auto& e : input.items[0].enchs)
                src_vec.push_back(e);
            input.data = std::move(src_vec);
        } else if constexpr (std::is_same_v<T, InventoryPayload>) {
            // Inventory mode: items[0] = equipment with its current enchants
            // (target_item.enchantments ARE the current state in inventory mode)
            algorithm::Item equip_item = input.target;
            input.items.push_back(std::move(equip_item));

            // Convert extra_items
            algorithm::ItemCollection inv_items;
            for (const auto& item : payload.extra_items) {
                algorithm::Item algo_item;
                algo_item.type = item.is_book()
                    ? algorithm::ItemType::Book
                    : algorithm::ItemType::Equip;
                algo_item.ppn = static_cast<uint8_t>(item.prior_penalty);
                algo_item.dur = static_cast<int16_t>(item.durability);
                algo_item.enchs = remap_nsid_to_local(item.enchantments);
                input.items.push_back(algo_item);
                inv_items.push_back(std::move(algo_item));
            }

            // Data union: extra items as ItemCollection
            input.data = std::move(inv_items);
            input.priorities = payload.extra_item_priorities;
        }
    }, request.payload);

    return input;
}

// ============================================================================
// recall — algorithm -> business (compact -> domain)
// ============================================================================

std::vector<Solution> CompactAdapter::recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& input)
{
    if (!output.is_valid)
        return {};

    // ── 1. Reconstruct business types from AlgorithmInput ───────────────

    // original_ench: from input.source() for direct mode
    EnchSet original_ench;
    if (input.is_direct()) {
        for (const auto& e : input.source()) {
            NSID nsid = input.ench_reg.to_global_id(e.id);
            original_ench.emplace(nsid, nsid.str(), e.level);
        }
    }

    // target_item: from input.target via to_domain()
    Item target_item = to_domain(input.target, input.ench_reg);

    // available_items: from input.inventory_items() for inventory mode
    ItemCollection available_items;
    if (input.is_inventory()) {
        for (const auto& item : input.inventory_items())
            available_items.push_back(to_domain(item, input.ench_reg));
    }

    // ── 2. Convert each compact solution ───────────────────────────────
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
                output.algorithm_name, output.algorithm_version,
                std::chrono::system_clock::now(),
                std::chrono::milliseconds(0)
            }
        ));
    }

    return solutions;
}

// ============================================================================
// from_domain  —  business Item -> algorithm Item
// ============================================================================

algorithm::Item CompactAdapter::from_domain(
    const Item& item,
    const algorithm::EnchReg& reg)
{

    algorithm::Item citem;
    citem.type = item.is_book() ? algorithm::ItemType::Book
                                : algorithm::ItemType::Equip;
    citem.ppn  = static_cast<uint8_t>(item.prior_penalty);
    citem.dur  = static_cast<int16_t>(item.durability);

    for (const auto& ench : item.enchantments) {
        int16_t local_id = reg.to_local_id(ench.id);
        if (local_id >= 0)
            citem.enchs.insert(algorithm::Ench{local_id,
                                static_cast<int16_t>(ench.level)});
    }
    return citem;
}

// ============================================================================
// to_domain  —  algorithm Item -> business Item
// ============================================================================

Item CompactAdapter::to_domain(
    const algorithm::Item& item,
    const algorithm::EnchReg& reg)
{
    EnchSet ench_set;
    for (const auto& e : item.enchs) {
        NSID nsid = reg.to_global_id(e.id);
        ench_set.emplace(nsid, nsid.str(), e.level);
    }

    if (item.type == algorithm::ItemType::Book) {
        return Item(NSID("minecraft:enchanted_book"), ench_set, item.ppn);
    } else {
        const auto& equip = reg.get_target_equip();
        NSID eq_id(std::to_string(equip.id));
        return Item(eq_id, ench_set, item.ppn, item.dur);
    }
}
