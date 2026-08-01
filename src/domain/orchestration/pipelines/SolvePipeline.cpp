#include "SolvePipeline.h"
#include "domain/business/types/Profile.h"
#include "domain/business/components/TagResolver.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

/// Build a TagResolver from the profile's equipment → category mapping when
/// the profile does not carry an explicit resolver.  Each equipment id is
/// recorded as a member of its `#tag` category, reproducing the legacy
/// category-match semantics for profiles loaded without tag membership data.
TagResolver fallback_tag_resolver(const Profile &profile) {
    TagResolver tr;
    std::unordered_map<std::string, std::unordered_set<std::string>> members;
    for (const auto &[id, eq] : profile.eq().data()) {
        if (!eq.category.is_tag())
            continue;
        members[eq.category.str().substr(1)].insert(id.str());
    }
    for (const auto &[key, vals] : members)
        tr.add_tag(key, vals);
    return tr;
}

} // namespace

SolveResult SolvePipeline::run(
    Profile& profile,
    const SolveRequest& request,
    algorithm::AlgorithmLoader& loader,
    algorithm::AlgorithmExecutor** out_executor)
{
    // Stage 1: Apply
    auto s1 = stage_apply(profile, request);

    // Stage 2: Execute
    auto s2 = stage_execute(s1.algo_input, request.algorithm, loader, out_executor);

    // Short-circuit if no output
    if (!s2.algo_output.is_valid) {
        SolveResult empty;
        empty.algorithm_used = s2.algorithm_name;
        empty.computation_time_ms = s2.computation_time_ms;
        return empty;
    }

    // Stage 3: Recall
    auto result = stage_recall(s2.algo_output, s1.algo_input);
    result.algorithm_used = s2.algorithm_name;
    result.computation_time_ms = s2.computation_time_ms;
    return result;
}

SolvePipeline::Stage1Result SolvePipeline::stage_apply(
    const Profile& profile,
    const SolveRequest& request)
{
    Stage1Result result;
    const TagResolver *tr = profile.tag_resolver();
    TagResolver fallback;
    if (!tr) {
        fallback = fallback_tag_resolver(profile);
        tr       = &fallback;
    }
    result.algo_input = CompactAdapter::apply(profile, request, *tr);
    result.target_eq_nsid = request.target_item.id;
    return result;
}

SolvePipeline::Stage2Result SolvePipeline::stage_execute(
    algorithm::AlgorithmInput& algo_input,
    const std::string& algorithm,
    algorithm::AlgorithmLoader& loader,
    algorithm::AlgorithmExecutor** out_executor)
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
    if (!(algo->supported_mode() & algo_input.config.mode)) {
        std::string mode_str = (algo_input.config.mode == AlgorithmMode::inventory)
            ? "inventory" : "direct";
        throw std::runtime_error(tr_fmt("pipeline.err.unsupported_mode", algorithm, mode_str));
    }

    // Feasibility gate (cheap).  The resolver (which produces the strategy's
    // working item set) is called by the strategy itself inside execute().
    result.algorithm_name = algorithm;
    if (!algo->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable");
        return result;
    }

    // Execute
    auto start = std::chrono::steady_clock::now();
    algorithm::AlgorithmExecutor executor(std::move(algo));
    // Expose executor for cross-thread cancellation (besq_abort_solve).
    // out_executor stays valid until stage_execute returns.
    if (out_executor) *out_executor = &executor;
    executor.start(algo_input);
    executor.wait();
    if (out_executor) *out_executor = nullptr;
    auto end = std::chrono::steady_clock::now();

    result.computation_time_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(end - start).count();
    result.algo_output = executor.output();
    return result;
}

SolveResult SolvePipeline::stage_recall(
    const algorithm::AlgorithmOutput& output,
    const algorithm::AlgorithmInput& algo_input)
{
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    result.solutions = CompactAdapter::recall(output, algo_input);
    result.success = !result.solutions.empty();
    return result;
}
