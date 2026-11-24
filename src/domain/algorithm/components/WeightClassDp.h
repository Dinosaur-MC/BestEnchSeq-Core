#pragma once
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace algorithm {
class ExecutionContext;
struct SearchConfig;

/// Weight-class DP — exact direct-mode fast path shared by dp_merge and bb_dp.
///
/// ## When it applies
///
/// The working set is one equipment base (items[0]) plus books whose
/// enchantments are pairwise disjoint, mutually compatible, compatible with the
/// base and applicable to it, each book carrying a target enchant the base
/// still lacks — exactly what DefaultResolver emits in direct mode (and in
/// inventory mode when the pool has no spare items).  Every forge cost then decomposes into per-book constants:
///
///     book  ← group : P(h_base) + P(h_group) + Σ_{b ∈ group} w_book(b)
///     equip ← group : P(h_base) + P(h_group) + Σ_{b ∈ group} w_base(b)
///
/// where P(h) = 2^h − 1 and w_book / w_base are measured once per book through
/// ForgeEngine (so Java/Bedrock rules, same-level upgrades of a source enchant,
/// and ignore_* flags all come from the engine itself).
///
/// ## Why it is fast
///
/// Books with equal (w_book, w_base, ppn) are interchangeable, so the partition
/// DP runs over sub-multisets — a count per weight class — instead of subsets:
/// states shrink from 2^k to Π(c_j + 1) and splits from 3^k to
/// Π(c_j + 1)(c_j + 2)/2.  Each state keeps a (height → min cost) Pareto
/// frontier; future cost depends only on (multiset, height), so the optimum is
/// the one the subset DPs (dp_merge / bb_dp) find.  The per-step cap prunes
/// forges exactly as bb_dp's Pass A does.
///
/// ## Safety net
///
/// The chosen tree is replayed through ForgeEngine::forge_into to build the
/// reported steps.  If the replayed cost differs from the DP cost or the final
/// item misses the target, the fast path reports Ineligible and the caller runs
/// its general search — the fast path can never return a wrong plan.
class WeightClassDp {
public:
    enum class Objective : uint8_t {
        /// dp_merge: cheapest plan; among equal-cost plans prefer one whose
        /// every step is ≤ cap (the optimum cost never changes).
        MinCost,
        /// bb_dp: cheapest plan whose every step is ≤ cap; when none exists,
        /// the cheapest plan overall (bb_dp's Pass A / Pass B semantics).
        CapFirst,
    };

    enum class Status : uint8_t {
        Ineligible, ///< input shape not covered, over budget, or replay mismatch — run the general search
        Solved,     ///< `steps` holds an optimal plan
        Cancelled,  ///< ctx was cancelled mid-pass
    };

    /// Max partition splits per DP pass before the fast path declines (keeps a
    /// pass well under a second; larger inputs keep the general search, which
    /// owns checkpointing for long runs).
    static constexpr uint64_t kDefaultSplitBudget = 64'000'000;

    struct Options {
        Objective objective = Objective::MinCost;
        int32_t max_step_cost = 0; ///< per-step cap, ≤ 0 = none
        uint64_t split_budget = kDefaultSplitBudget;
    };

    struct Result {
        Status status = Status::Ineligible;
        std::vector<EnchStep> steps;
        int64_t cost = 0;
        bool relaxed = false;      ///< CapFirst: no ≤cap plan existed, unconstrained optimum returned
        uint32_t classes = 0;      ///< number of weight classes
        uint64_t states = 0;       ///< multiset states per pass, Π(c_j + 1)
        uint64_t subproblems = 0;  ///< states with a non-empty frontier (last pass)
        uint32_t max_frontier = 0; ///< largest frontier (last pass)
        uint64_t cap_pruned = 0;   ///< forges rejected by the cap (all passes)

        /// Copy the run's figures into the caller's partition-DP diagnostics
        /// (status is left to the caller).
        void fill_diagnostics(PartitionDpDiagnostics& d) const noexcept {
            d.solution_cost = static_cast<int32_t>(cost);
            d.dp_weight_classes = classes;
            d.dp_subproblems_solved = subproblems;
            d.dp_cache_slots = states;
            d.dp_cache_hits = 0;
            d.dp_max_frontier_size = max_frontier;
            d.dp_cap_pruned = cap_pruned;
            d.dp_pass_b_ran = relaxed;
            d.normalized_explored_states = static_cast<int64_t>(subproblems);
        }
    };

    /// Opt-out switch: SearchConfig.extra["<strategy>.weight_class"] set to
    /// "off", "0" or "false" disables the fast path for that strategy.
    static bool enabled(const SearchConfig& cfg, std::string_view strategy);

    /// \p items is the resolved working set (normalize_base_equipment applied).
    static Result solve(const ItemCollection& items,
                        const Item& target,
                        const EnchReg& reg,
                        const ForgeEngine& engine,
                        const Options& opt,
                        ExecutionContext& ctx);
};

} // namespace algorithm
