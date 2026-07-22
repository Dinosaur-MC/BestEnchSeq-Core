#pragma once
#include "./components/SearchUtils.h"
#include "types/AlgorithmTypes.h"
#include <cstdint>
#include <vector>

class ExecutionContext;
class IAlgorithmSerializer;

// ─── IAlgorithm (pure interface, compact-only) ───
class IAlgorithm {
  public:
    virtual ~IAlgorithm() = default;

    virtual std::string_view name() const noexcept    = 0;
    virtual std::string_view version() const noexcept = 0;

    virtual void execute(const AlgorithmInput &input, ExecutionContext &ctx) = 0;

    /// Returns the set of operation modes this algorithm supports.
    /// Default: direct mode only.
    /// Override e.g.:
    ///   `return AlgorithmMode::direct | AlgorithmMode::inventory;`
    virtual AlgorithmMode supported_mode() const noexcept { return AlgorithmMode::direct; }

    /// Quick feasibility check: returns true if the target is reachable
    /// from the given items without computing exact costs.
    /// Default: pessimistic but catches trivial cases (empty items, target
    /// already met, no books to work with).  Strategies may override for
    /// tighter checks (e.g., greedy pure-forge in GreedyAlgorithm).
    virtual bool simulate(const AlgorithmInput &input) const noexcept {
        if (input.items.empty())
            return false;
        if (meets_target(input.items[0], input.target))
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
