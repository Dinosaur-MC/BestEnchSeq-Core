#pragma once
#include "types/AlgorithmTypes.h"
#include <cstdint>
#include <vector>

class ExecutionContext;
class IAlgorithmSerializer;

// ─── IAlgorithm (pure interface, compact-only) ───
class IAlgorithm {
  public:
    virtual ~IAlgorithm() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;

    virtual void execute(const AlgorithmInput &input, ExecutionContext &ctx) = 0;

    /// Quick feasibility check: returns true if the target is reachable
    /// from the given items without computing exact costs.
    /// Default returns true (pessimistic).  Strategies that implement this
    /// provide fast pre-filtering for inventory mode.
    virtual bool simulate(const AlgorithmInput &input) const noexcept { (void)input; return true; }

    /// Whether this algorithm supports checkpoint serialization for resume.
    /// Default returns false.  Resumable algorithms must override to return true.
    virtual bool is_resumable() const noexcept { return false; }

    /// Access the full AlgorithmInput.  Used by serializer base class to
    /// read/write checkpoint data without knowing the concrete algorithm type.
    /// Default returns a static empty reference.
    virtual const AlgorithmInput& algorithm_input_ref() const noexcept {
        static const AlgorithmInput empty{};
        return empty;
    }

    /// Replace the stored AlgorithmInput (e.g. after deserializing a checkpoint).
    /// Default does nothing.  Resumable algorithms must override to store.
    virtual void set_algorithm_input(AlgorithmInput input) noexcept {
        (void)input;
    }

    /// Returns the associated serializer for this algorithm's state.
    /// Returns nullptr if the algorithm does not support serialization.
    virtual IAlgorithmSerializer* get_serializer() noexcept { return nullptr; }
    virtual const IAlgorithmSerializer* get_serializer() const noexcept { return nullptr; }
};
