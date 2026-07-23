#pragma once
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ResolverTypes.h"

namespace algorithm {
class ExecutionContext;
class IAlgorithmSerializer;

// ─── IAlgorithm (pure interface, compact-only) ───
class IAlgorithm {
  public:
    virtual ~IAlgorithm() = default;

    virtual std::string_view name() const noexcept    = 0;
    virtual std::string_view version() const noexcept = 0;

    virtual void execute(const AlgorithmInput &input, ExecutionContext &ctx) = 0;

    /// Pre-process input before execution.
    ///
    /// Direct mode: reads source enchants from items[0].enchs and desired
    /// from target, computes diff, generates graduated books.
    /// Inventory mode: reads items[1..] as available pool, sorts by
    /// priorities, filters by reachability.
    ///
    /// Returns generated/filtered items.  Empty = unreachable / no work
    /// needed.  The caller appends the result to input.items.
    virtual ResolverOutput resolve(const AlgorithmInput &input) const;

    /// Returns the set of operation modes this algorithm supports.
    /// Default: direct mode only.
    virtual AlgorithmMode supported_mode() const noexcept { return AlgorithmMode::direct; }

    /// Quick feasibility check: returns true if the target is reachable
    /// from the given items without computing exact costs.
    /// Default: pessimistic but catches trivial cases (empty items, target
    /// already met, no books to work with).  Strategies may override for
    /// tighter checks (e.g., greedy pure-forge in GreedyAlgorithm).
    virtual bool simulate(const AlgorithmInput &input) const noexcept {
        if (input.items.empty())
            return false;
        if (meets_target(input.items[0], input.target.enchs))
            return true;
        return input.items.size() > 1;
    }

    /// Whether this algorithm supports checkpoint serialization for resume.
    /// Default returns false.  Resumable algorithms must override to return true.
    virtual bool is_resumable() const noexcept { return false; }

    /// Returns the associated serializer for this algorithm's state.
    /// Returns nullptr if the algorithm does not support serialization.
    virtual IAlgorithmSerializer *get_serializer() noexcept { return nullptr; }
    virtual const IAlgorithmSerializer *get_serializer() const noexcept { return nullptr; }
};

} // namespace algorithm
