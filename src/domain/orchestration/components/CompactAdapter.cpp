#include "CompactAdapter.h"
#include "domain/business/components/TagResolver.h"
#include "common/CommonTypes.h"
#include "common/i18n/Language.h"
#include "common/utils/ExpCalculator.hpp"

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

// ============================================================================
// is_supported — shared tag-membership applicability predicate
// ============================================================================

bool CompactAdapter::is_supported(const EnchInfo &info, const NSID &item_id,
                                  const std::unordered_set<NSID> &item_tags) {
    if (info.supported_items.contains(item_id))
        return true;
    for (const auto &t : info.supported_items) {
        if (t.is_tag() && item_tags.contains(t))
            return true;
    }
    return false;
}

// ============================================================================
// apply — Profile + SolveRequest -> AlgorithmInput
// ============================================================================

algorithm::AlgorithmInput CompactAdapter::apply(const Profile &profile, const SolveRequest &request,
                                                const TagResolver &tag_resolver) {
    // ── 1. Resolve registries and equipment ─────────────────────────────
    const auto &ench_registry = profile.ench();
    const auto &eq_registry   = profile.eq();

    // Look up target equipment by item id.  Books may not be in the
    // equipment registry — fall back to a minimal placeholder.
    const bool is_book = request.target_item.is_book();
    ::Equipment target_eq;
    if (is_book) {
        target_eq = ::Equipment{request.target_item.id, request.target_item.id.str(), NSID(), 0};
    } else {
        try {
            target_eq = eq_registry.at(request.target_item.id);
        } catch (const std::out_of_range &) {
            target_eq = ::Equipment{request.target_item.id, request.target_item.id.str(), NSID(), 0};
        }
    }

    // ── 2. Build algorithm Equipment ────────────────────────────────────
    algorithm::Equipment algo_equip;
    algo_equip.id             = target_eq.id;
    algo_equip.max_durability = target_eq.max_durability;

    // ── 3. Sort full registry by NSID (deterministic global_id) ─────────
    const auto &all_infos_map = ench_registry.data();
    std::vector<std::pair<NSID, EnchInfo>> sorted_infos;
    sorted_infos.reserve(all_infos_map.size());
    for (const auto &[nsid, info] : all_infos_map)
        sorted_infos.emplace_back(nsid, info);
    std::sort(sorted_infos.begin(), sorted_infos.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    // ── 4. Filter applicable enchantments, build compact EnchReg ───────
    std::vector<algorithm::EnchInfo> algo_infos;
    std::vector<NSID> applicable_global_nsids;
    std::unordered_map<NSID, int16_t> nsid_to_local;

    // Applicability of each enchantment to the target is decided by the shared
    // is_supported predicate (concrete id hit OR `#tag` ∩ tags_of).  Hoist the
    // target's tag set once — it is target-wide, not per-enchantment.
    const std::unordered_set<NSID> target_tags =
        tag_resolver.tags_of(request.target_item.id.str());

    for (size_t gid = 0; gid < sorted_infos.size() && gid < 64; ++gid) {
        const auto &biz = sorted_infos[gid].second;

        // Platform availability: an enchantment restricted to one platform is
        // excluded from a solve targeting the other.  None/All = everywhere.
        // (Requested platform-incompatible enchants are then caught by the
        // validate_enchants -> nsid_to_local lookup below.)
        if (!(biz.supported_platform == MCE::None ||
              biz.supported_platform == MCE::All ||
              biz.supported_platform == request.forge_config.platform))
            continue;

        // A book target accepts every enchantment: in MC a book becomes an
        // enchanted_book when enchanted, and an enchanted book can hold any
        // enchantment.  Mirrors the inventory-item book exception below.
        if (!request.target_item.is_book() &&
            !is_supported(biz, request.target_item.id, target_tags))
            continue;

        algorithm::EnchInfo ai;
        ai.id      = static_cast<uint8_t>(algo_infos.size());
        ai.mul     = static_cast<uint8_t>(biz.multiplier);
        ai.mul_b   = static_cast<uint8_t>(std::max(1, biz.multiplier >> 1));
        ai.max_lvl = static_cast<uint8_t>(biz.max_level);
        for (size_t local_idx = 0; local_idx < algo_infos.size() && local_idx < 64; ++local_idx) {
            if (biz.exclusive_set.contains(applicable_global_nsids[local_idx])) {
                ai.exc_mask |= (algorithm::mask_type(1) << local_idx);
            }
        }
        ai.applicable = true;

        int16_t local_id                       = static_cast<int16_t>(algo_infos.size());
        nsid_to_local[sorted_infos[gid].first] = local_id;
        applicable_global_nsids.push_back(sorted_infos[gid].first);
        algo_infos.push_back(std::move(ai));
    }

    // ── 5. Init compact registry ───────────────────────────────────────
    // Populate applicable local enchantment IDs on the equipment.
    algo_equip.applicable_enchs.reserve(algo_infos.size());
    for (uint8_t i = 0; i < algo_infos.size(); ++i)
        algo_equip.applicable_enchs.insert(i);

    algorithm::EnchReg ench_reg;
    ench_reg.init(std::move(algo_infos), std::move(applicable_global_nsids), algo_equip);

    // ── 6. NSID -> local_id remap helper ───────────────────────────────
    auto remap_nsid_to_local = [&](const EnchSet &src) -> algorithm::EnchSet {
        algorithm::EnchSet dst;
        for (const auto &e : src) {
            auto it = nsid_to_local.find(e.id);
            if (it != nsid_to_local.end())
                dst.insert(algorithm::Ench{static_cast<algorithm::Ench::value_type>(it->second),
                                           static_cast<algorithm::Ench::value_type>(e.level)});
        }
        return dst;
    };

    // ── 6b. Requested-enchantment validation ─────────────────────────
    // Enchantments requested for the target equipment (desired enchants in
    // direct mode, current state in inventory mode) must be applicable to
    // it and must not exceed the registry's max level.  Both were previously
    // accepted silently (inapplicable ones dropped by remap_nsid_to_local,
    // over-level ones forwarded unchanged), producing impossible plans;
    // report them instead of hiding the data loss.  IDs absent from the
    // global registry are ignored for backward compatibility (legacy
    // behavior).
    auto validate_enchants = [&](const EnchSet &src) {
        for (const auto &e : src) {
            auto it = all_infos_map.find(e.id);
            if (it == all_infos_map.end())
                continue;  // unknown id → ignore
            if (nsid_to_local.find(e.id) == nsid_to_local.end())
                throw std::runtime_error(tr_fmt("main.err.ench_not_applicable",
                                                e.id.str(),
                                                request.target_item.id.str()));
            if (e.level > it->second.max_level)
                throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max",
                                                e.id.str(), e.level,
                                                it->second.max_level));
        }
    };

    // ── 6c. Inventory-item validation ─────────────────────────────────
    // Each inventory item's enchantments must not exceed the registry max
    // level; an enchantment carried by an equipment item must additionally
    // be applicable to that item via the same is_supported predicate used by
    // the target filter above.  Books accept any enchantment, so they skip
    // the applicability check.
    auto validate_inventory_item = [&](const Item &item) {
        // Tag membership is per-item, not per-enchantment — hoist out of the loop.
        const auto item_tags = tag_resolver.tags_of(item.id.str());
        for (const auto &e : item.enchantments) {
            auto it = all_infos_map.find(e.id);
            if (it == all_infos_map.end())
                continue;  // unknown id → ignore (legacy behavior)
            if (e.level > it->second.max_level)
                throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max",
                                                e.id.str(), e.level,
                                                it->second.max_level));
            if (item.is_book())
                continue;  // books accept any enchantment
            if (!eq_registry.contains(item.id))
                continue;  // unknown equipment → skip applicability check
            if (!is_supported(it->second, item.id, item_tags))
                throw std::runtime_error(tr_fmt("main.err.ench_not_applicable",
                                                e.id.str(), item.id.str()));
        }
    };

    // ── 7. Build AlgorithmInput skeleton ───────────────────────────────
    algorithm::AlgorithmInput input;
    input.registry      = std::move(ench_reg);
    input.config.mode   = request.mode;
    input.config.forge  = request.forge_config;
    input.config.search = request.search_config;

    // Convert target_item (domain -> algorithm)
    // Direct mode: target_item.enchantments = desired enchants (must be non-empty)
    if (request.mode == AlgorithmMode::direct &&
        request.target_item.enchantments.empty())
    {
        throw std::runtime_error(
            tr("main.err.target_no_enchants"));
    }
    validate_enchants(request.target_item.enchantments);
    algorithm::Item algo_target;
    algo_target.type  = is_book ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
    algo_target.ppn   = static_cast<uint8_t>(request.target_item.prior_penalty);
    algo_target.dur   = static_cast<int16_t>(request.target_item.durability);
    algo_target.enchs = remap_nsid_to_local(request.target_item.enchantments);
    input.target      = std::move(algo_target);

    // ── 8. Convert payload ─────────────────────────────────────────────
    std::visit(
        [&](const auto &payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, DirectPayload>) {
                // Direct mode: data = the equipment's current enchantments.
                // (The base equipment itself is assembled by the resolver
                // from input.target + this source.)
                validate_enchants(payload.source_enchantments);
                algorithm::EnchCollection src_vec;
                for (const auto &e : remap_nsid_to_local(payload.source_enchantments))
                    src_vec.push_back(e);
                input.data = algorithm::DirectPayload{std::move(src_vec)};
            } else if constexpr (std::is_same_v<T, InventoryPayload>) {
                // Inventory mode: available = all enchanted books + all
                // equipment items from the input (empty books dropped).
                // No equipment-first guarantee — the resolver/strategy
                // selects its own base equipment via Item::type.
                const auto &extra = payload.extra_items;
                const auto &prios = payload.extra_item_priorities;
                algorithm::ItemCollection avail;
                std::vector<int32_t> inv_prios;
                avail.reserve(extra.size());
                inv_prios.reserve(extra.size());
                // 守卫：inventory 模式必须有非空 target。空 target 会让下方
                // 异类过滤静默排除全部装备（item.id != "" 恒真），产出空池。
                if (request.target_item.id.str().empty())
                    throw std::runtime_error("inventory mode requires a target item");
                for (size_t i = 0; i < extra.size(); ++i) {
                    const auto &item = extra[i];
                    if (item.is_book() && item.enchantments.empty())
                        continue;  // drop empty books (no forge value)
                    // 异类装备过滤：非目标 id 的装备从池中排除（SRS 语义）——
                    // 避免异类装备被选为 base 产出错误方案（compact Item 无装备 NSID 无法区分）。
                    // 目标为书时无匹配装备——全部装备排除。
                    if (!item.is_book() &&
                        (request.target_item.is_book() || item.id != request.target_item.id))
                        continue;
                    validate_inventory_item(item);
                    algorithm::Item algo_item;
                    algo_item.type  = item.is_book() ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
                    algo_item.ppn   = static_cast<uint8_t>(item.prior_penalty);
                    algo_item.dur   = static_cast<int16_t>(item.durability);
                    algo_item.enchs = remap_nsid_to_local(item.enchantments);
                    avail.push_back(std::move(algo_item));
                    inv_prios.push_back(i < prios.size() ? prios[i] : 99);
                }
                input.data = algorithm::InventoryPayload{std::move(avail), std::move(inv_prios)};
            }
        },
        request.payload
    );

    return input;
}

// ============================================================================
// recall — algorithm -> business (compact -> domain)
// ============================================================================

std::vector<Solution>
CompactAdapter::recall(const algorithm::AlgorithmOutput &output, const algorithm::AlgorithmInput &input) {
    if (!output.is_valid)
        return {};

    // ── 1. Reconstruct business types from AlgorithmInput ───────────────

    // original_ench: from input.source() for direct mode
    EnchSet original_ench;
    if (input.is_direct()) {
        for (const auto &e : input.source()) {
            NSID nsid = input.registry.to_global_id(e.id);
            original_ench.emplace(nsid, std::string{}, e.level);
        }
    }

    // target_item: from input.target via to_domain() with equipment NSID
    Item target_item = to_domain(input.target, input.registry);

    // available_items: from inventory items or extracted from steps
    ItemCollection available_items;
    if (input.is_inventory()) {
        for (const auto &item : input.available())
            available_items.push_back(to_domain(item, input.registry));
    }
    // Note: direct mode has no available_items — source enchantments are
    // shown in the Forge Plan header via original_ench.

    // ── 2. Convert each compact solution ───────────────────────────────
    std::vector<Solution> solutions;
    solutions.reserve(output.solutions.size());

    // Convert final_item (compact → domain) once, shared by all solutions
    std::optional<Item> domain_final_item;
    if (output.final_item.type != algorithm::ItemType::Book ||
        !output.final_item.enchs.empty() || output.final_item.ppn != 0) {
        domain_final_item = to_domain(output.final_item, input.registry);
    }

    for (const auto &csol : output.solutions) {
        std::vector<Solution::EnchStep> domain_steps;
        domain_steps.reserve(csol.steps.size());

        for (const auto &s : csol.steps) {
            auto base = to_domain(s.base, input.registry);
            auto sac  = to_domain(s.sacrifice, input.registry);

            domain_steps.push_back(
                Solution::EnchStep{
                    std::move(base), std::move(sac), s.cost, ExpCalculator::level_to_exp(s.cost)
                }
            );
        }

        // Determine platform from forge config
        MCE plat = MCE::Java;
        if (input.config.forge.platform == MCE::Java)
            plat = MCE::Java;
        else if (input.config.forge.platform == MCE::Bedrock)
            plat = MCE::Bedrock;
        else
            plat = MCE::Java;

        auto sol = Solution::make(
            plat, original_ench, target_item, available_items, domain_steps, true,
            Solution::MetaData{
                output.algorithm_name, output.algorithm_version, output.created_at,
                output.computation_time
            }
        );
        sol.final_item = domain_final_item;  // set final item (shared by all solutions)
        solutions.push_back(std::move(sol));
    }

    return solutions;
}

// ============================================================================
// from_domain  —  business Item -> algorithm Item
// ============================================================================

algorithm::Item CompactAdapter::from_domain(const Item &item, const algorithm::EnchReg &reg) {

    algorithm::Item citem;
    citem.type = item.is_book() ? algorithm::ItemType::Book : algorithm::ItemType::Equip;
    citem.ppn  = static_cast<uint8_t>(item.prior_penalty);
    citem.dur  = static_cast<int16_t>(item.durability);

    for (const auto &ench : item.enchantments) {
        try {
            auto local_id = reg.to_local_id(ench.id);
            citem.enchs.insert(algorithm::Ench{local_id,
                                               static_cast<algorithm::Ench::value_type>(ench.level)});
        } catch (const std::out_of_range &) {
            // enchantment not applicable to target — skip
        }
    }
    return citem;
}

// ============================================================================
// to_domain  —  algorithm Item -> business Item
// ============================================================================

Item CompactAdapter::to_domain(const algorithm::Item &item, const algorithm::EnchReg &reg) {
    EnchSet ench_set;
    for (const auto &e : item.enchs) {
        NSID nsid = reg.to_global_id(e.id());
        ench_set.emplace(nsid, std::string{}, e.level());
    }

    if (item.type == algorithm::ItemType::Book) {
        return Item(NSID("minecraft:enchanted_book"), ench_set, item.ppn);
    } else {
        return Item(reg.get_target_equip().id, ench_set, item.ppn, item.dur);
    }
}
