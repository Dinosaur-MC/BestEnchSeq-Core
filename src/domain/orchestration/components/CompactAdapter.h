#pragma once
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/orchestration/types/SolveRequest.h"
#include <vector>

struct CompactAdapter {
    /// Build AlgorithmInput from Profile + SolveRequest.
    /// Internally builds EnchReg with correct NSID -> local_id mapping,
    /// eliminating the previous ench.id = 0 TEMP workaround.
    static algorithm::AlgorithmInput apply(
        const Profile& profile,
        const SolveRequest& request
    );

    /// Convert compact algorithm output back to domain Solution list.
    /// Reconstructs original_ench, target_item, and available_items from
    /// the AlgorithmInput itself.
    static std::vector<Solution> recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& input,
        const NSID& target_eq_nsid = {}
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
        const algorithm::EnchReg& reg,
        const NSID& target_eq_nsid = {}
    );
};
