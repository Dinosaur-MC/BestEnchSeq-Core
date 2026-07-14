#pragma once
#include "types/AlgorithmTypes.h"
#include <cstdint>
#include <vector>

class ExecutionContext;

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

    virtual bool is_resumable() const noexcept { return false; }
    virtual std::vector<uint8_t> serialize_state() const { return {}; }
    virtual void deserialize_state(const std::vector<uint8_t> &) {}
};
