#pragma once
#include "api/SolvePipeline.h"
#include "besq/besq.h"
#include "types/AlgorithmTypes.h"

/// Internal extension of BesqContext that exposes the layered solve pipeline.
///
/// Each method corresponds to one stage of the pipeline, allowing callers
/// to inspect or modify intermediate results (ResolvedInput, AlgorithmInput,
/// AlgorithmOutput) — all of which are internal types.
///
/// Ordinary API consumers should use BesqContext::solve() instead.
class BesqContextInternal : public BesqContext {
public:
    /// Stage 1: Domain resolution — validates inputs and generates graduated
    /// books for direct mode.
    ResolvedInput resolve(const SolveInput& input) const;

    /// Stage 2: Domain -> compact conversion (via CompactAdapter).
    AlgorithmInput apply(const ResolvedInput& resolved) const;

    /// Stage 3: Compact algorithm execution — returns the internal
    /// ExecuteResult which carries the AlgorithmOutput needed by recall().
    detail::ExecuteResult execute(AlgorithmInput& algo_input,
                                  const std::string& algorithm);

    /// Stage 4: Compact -> domain conversion (via CompactAdapter::recall)
    /// wrapped in a SolveResult.
    SolveResult recall(const AlgorithmOutput& output,
                       const AlgorithmInput& algo_input,
                       const ResolvedInput& resolved) const;
};
