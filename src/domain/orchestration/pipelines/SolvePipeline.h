#pragma once
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/orchestration/types/SolveRequest.h"
#include "domain/orchestration/types/SolveResult.h"

#include <atomic>
#include <memory>

namespace orchestration {
class SolveSnapshot;
} // namespace orchestration
namespace algorithm {
class AlgorithmLoader;
class IExecutor;
} // namespace algorithm

/// Shared handle to the in-flight executor (AlgorithmExecutor or
/// SandboxedExecutor — the algorithm domain's authoritative interface).
/// stage_execute stores the executor here (under the atomic) so a different
/// thread (besq_abort_solve) can cancel() it while it runs; stage_execute
/// clears it when the executor finishes.  The shared_ptr copy taken by an
/// aborting thread keeps the executor alive for the duration of cancel(), so
/// the handle can never dangle (B-T22: replaces a raw executor* that raced +
/// dangled).
using ActiveExecutorHandle = std::atomic<std::shared_ptr<algorithm::IExecutor>>;

struct SolvePipeline {
    /// Run the full solve pipeline: apply -> execute -> recall.
    /// @param out_executor  Optional — set to the shared executor handle during
    ///                      stage_execute; cleared after stage_execute returns.
    ///                      Use for cross-thread cancellation (besq_abort_solve).
    static SolveResult run(const orchestration::SolveSnapshot& snapshot,
                           const SolveRequest& request,
                           algorithm::AlgorithmLoader& loader,
                           ActiveExecutorHandle* out_executor = nullptr);

    // Stage helpers exposed for targeted testing.
    struct Stage1Result {
        algorithm::AlgorithmInput algo_input;
        NSID target_eq_nsid; // equipment NSID for round-trip recall
    };
    struct Stage2Result {
        algorithm::AlgorithmOutput algo_output;
        int64_t computation_time_ms = 0;
        std::string algorithm_name;
    };

    static Stage1Result stage_apply(const orchestration::SolveSnapshot& snapshot, const SolveRequest& request);
    static Stage2Result stage_execute(algorithm::AlgorithmInput& algo_input,
                                      const std::string& algorithm,
                                      algorithm::AlgorithmLoader& loader,
                                      ActiveExecutorHandle* out_executor = nullptr);
    static SolveResult stage_recall(const algorithm::AlgorithmOutput& output, const algorithm::AlgorithmInput& algo_input);
};
