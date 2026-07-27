#pragma once
#include "domain/orchestration/types/SolveRequest.h"
#include "domain/orchestration/types/SolveResult.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

class Profile;
namespace algorithm { class AlgorithmLoader; }

struct SolvePipeline {
    /// Run the full solve pipeline: apply -> execute -> recall.
    static SolveResult run(
        Profile& profile,
        const SolveRequest& request,
        algorithm::AlgorithmLoader& loader
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
        algorithm::AlgorithmLoader& loader
    );
    static SolveResult stage_recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& algo_input,
        const NSID& target_eq_nsid = {}
    );
};
