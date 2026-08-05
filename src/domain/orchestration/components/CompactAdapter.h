#pragma once
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/orchestration/types/SolveRequest.h"
#include "common/CommonTypes.h"
#include <unordered_set>
#include <vector>

class TagResolver;

struct CompactAdapter {
  public:
    /// Enchantment E applies to item I iff I.id ∈ E.supported_items (concrete)
    /// OR some `#tag` t ∈ E.supported_items is a member of I's tag set
    /// (tag intersection via TagResolver).  Shared by the target filter, the
    /// per-item inventory validation, and the enchantables web endpoint.
    static bool is_supported(const EnchInfo& info, const NSID& item_id,
                             const std::unordered_set<NSID>& item_tags);

    /// is_supported AND the platform gate: an enchantment restricted to one
    /// platform applies only to a solve/query on that platform (None/All =
    /// everywhere).  Single source of truth for the target filter and the
    /// enchantables web endpoint.
    static bool is_applicable(const EnchInfo& info, const NSID& item_id,
                              const std::unordered_set<NSID>& item_tags,
                              MCE platform);

    /// Build AlgorithmInput from Profile + SolveRequest.
    /// Internally builds EnchReg with correct NSID -> local_id mapping,
    /// eliminating the previous ench.id = 0 TEMP workaround.
    ///
    /// Applicability uses the MC tag-membership model: an enchantment E is
    /// applicable to target I iff
    ///   I.id ∈ E.supported_items                       (concrete item hit)
    ///   ∨ ∃ t ∈ E.supported_items: t is #tag ∧ t ∈ tag_resolver.tags_of(I.id)
    ///                                          (tag intersection)
    static algorithm::AlgorithmInput apply(
        const Profile& profile,
        const SolveRequest& request,
        const TagResolver& tag_resolver
    );

    /// Convert compact algorithm output back to domain Solution list.
    /// Reconstructs original_ench, target_item, and available_items from
    /// the AlgorithmInput itself.
    static std::vector<Solution> recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& input
    );

    /// Business Item -> algorithm Item (uses EnchReg::to_local_id for NSID mapping).
    static algorithm::Item from_domain(
        const Item& item,
        const algorithm::EnchReg& reg
    );

    /// algorithm Item -> business Item.
    /// @param target_eq_nsid  the target equipment NSID for round-trip;
    ///                        defaults to empty NSID (unknown fallback).
    static Item to_domain(
        const algorithm::Item& item,
        const algorithm::EnchReg& reg
    );
};
