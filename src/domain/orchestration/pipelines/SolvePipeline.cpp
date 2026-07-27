#include "SolvePipeline.h"
#include "domain/business/types/Profile.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include <chrono>

SolveResult SolvePipeline::run(
    Profile& profile,
    const SolveRequest& request,
    algorithm::AlgorithmLoader& loader)
{
    // Stage 1: Apply
    auto s1 = stage_apply(profile, request);

    // Stage 2: Execute
    auto s2 = stage_execute(s1.algo_input, request.algorithm, loader);

    // Short-circuit if no output
    if (!s2.algo_output.is_valid) {
        SolveResult empty;
        empty.algorithm_used = s2.algorithm_name;
        empty.computation_time_ms = s2.computation_time_ms;
        return empty;
    }

    // Stage 3: Recall
    auto result = stage_recall(s2.algo_output, s1.algo_input, s1.target_eq_nsid);
    result.algorithm_used = s2.algorithm_name;
    result.computation_time_ms = s2.computation_time_ms;
    return result;
}

SolvePipeline::Stage1Result SolvePipeline::stage_apply(
    const Profile& profile,
    const SolveRequest& request)
{
    Stage1Result result;
    result.algo_input = CompactAdapter::apply(profile, request);
    result.target_eq_nsid = request.target_item.id;
    return result;
}

SolvePipeline::Stage2Result SolvePipeline::stage_execute(
    algorithm::AlgorithmInput& algo_input,
    const std::string& algorithm,
    algorithm::AlgorithmLoader& loader)
{
    Stage2Result result;

    auto algo = loader.create(algorithm);
    if (!algo) {
        auto available = loader.list();
        {
            std::string avail_str;
            for (size_t i = 0; i < available.size(); ++i) {
                if (i > 0) avail_str += ", ";
                avail_str += available[i];
            }
            throw std::runtime_error(tr_fmt("pipeline.err.unknown_algo", algorithm, avail_str));
        }
    }

    // Check mode support
    if (!(algo->supported_mode() & algo_input.mode)) {
        std::string mode_str = (algo_input.mode == AlgorithmMode::inventory)
            ? "inventory" : "direct";
        throw std::runtime_error(tr_fmt("pipeline.err.unsupported_mode", algorithm, mode_str));
    }

    // Resolve: generate books or filter inventory
    auto resolved = algo->resolve(algo_input);
    result.algorithm_name = algorithm;

    if (resolved.empty()) {
        LOG_INFO("resolve: %s",
            (algo_input.mode == AlgorithmMode::inventory)
                ? "inventory unreachable" : "target already satisfied");
        return result;
    }

    size_t old_size = algo_input.items.size();
    algo_input.items.resize(old_size + resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i)
        algo_input.items[old_size + i] = std::move(resolved[i]);

    // Feasibility check
    if (!algo->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable");
        return result;
    }

    // Execute
    auto start = std::chrono::steady_clock::now();
    algorithm::AlgorithmExecutor executor(std::move(algo));
    executor.start(algo_input);
    executor.wait();
    auto end = std::chrono::steady_clock::now();

    result.computation_time_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(end - start).count();
    result.algo_output = executor.output();
    return result;
}

SolveResult SolvePipeline::stage_recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& algo_input,
    const NSID& target_eq_nsid)
{
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    result.solutions = CompactAdapter::recall(output, algo_input, target_eq_nsid);
    result.success = !result.solutions.empty();
    return result;
}
