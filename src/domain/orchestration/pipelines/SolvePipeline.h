#pragma once
#include "domain/orchestration/types/SolveRequest.h"
#include "domain/orchestration/types/SolveResult.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

class Profile;
namespace algorithm { class AlgorithmLoader; class AlgorithmExecutor; }

struct SolvePipeline {
    /// Run the full solve pipeline: apply -> execute -> recall.
    /// @param out_executor  Optional — set to the AlgorithmExecutor during
    ///                      stage_execute; cleared after stage_execute returns.
    ///                      Use for cross-thread cancellation (besq_abort_solve).
    static SolveResult run(
        const Profile& profile,
        const SolveRequest& request,
        algorithm::AlgorithmLoader& loader,
        algorithm::AlgorithmExecutor** out_executor = nullptr
    );

    // Stage helpers exposed for targeted testing.
    struct Stage1Result {
        algorithm::AlgorithmInput algo_input;
        NSID target_eq_nsid;                  // equipment NSID for round-trip recall
    };
    struct Stage2Result {
        algorithm::AlgorithmOutput algo_output;
        int64_t computation_time_ms = 0;
        std::string algorithm_name;
    };

    static Stage1Result stage_apply(
        const Profile& profile,
        const SolveRequest& request
    );
    static Stage2Result stage_execute(
        algorithm::AlgorithmInput& algo_input,
        const std::string& algorithm,
        algorithm::AlgorithmLoader& loader,
        algorithm::AlgorithmExecutor** out_executor = nullptr
    );
    static SolveResult stage_recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& algo_input
    );
};
