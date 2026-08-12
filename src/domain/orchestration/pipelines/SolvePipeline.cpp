#include "SolvePipeline.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "domain/algorithm/IExecutor.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/business/components/TagResolver.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/orchestration/types/SolveSnapshot.h"
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

/// RAII guard: clears the shared executor handle on scope exit (normal or
/// exceptional), so the owning BesqContext never retains a shared_ptr to an
/// executor whose local reference in stage_execute has been released.  An
/// abort thread that already copied the handle keeps the executor alive, so
/// cancel() remains safe even after the clear.
struct ExecutorHandleGuard {
    ActiveExecutorHandle* handle;
    ~ExecutorHandleGuard() {
        if (handle)
            handle->store(nullptr);
    }
};

/// Build a TagResolver from the snapshot's equipment → category mapping when
/// the snapshot does not carry an explicit resolver.  Each equipment id is
/// recorded as a member of its `#tag` category, reproducing the legacy
/// category-match semantics for profiles loaded without tag membership data.
/// TODO(T7/T10): attach a real TagResolver at profile load time (ProfileLoader
/// already builds the tag universe) so mod profiles with real-MC-tag
/// supported_items don't lose applicability via this category-derived fallback.
TagResolver fallback_tag_resolver(const orchestration::SolveSnapshot& snapshot) {
    TagResolver tr;
    std::unordered_map<std::string, std::unordered_set<std::string>> members;
    for (const auto& [id, eq] : snapshot.eq().data()) {
        if (!eq.category.is_tag())
            continue;
        members[eq.category.str().substr(1)].insert(id.str());
    }
    for (const auto& [key, vals] : members)
        tr.add_tag(key, vals);
    return tr;
}

} // namespace

SolveResult SolvePipeline::run(const orchestration::SolveSnapshot& snapshot,
                               const SolveRequest& request,
                               algorithm::AlgorithmLoader& loader,
                               ActiveExecutorHandle* out_executor) {
    // Stage 1: Apply
    auto s1 = stage_apply(snapshot, request);

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

SolvePipeline::Stage1Result SolvePipeline::stage_apply(const orchestration::SolveSnapshot& snapshot,
                                                       const SolveRequest& request) {
    Stage1Result result;
    const TagResolver* tr = &snapshot.tag_resolver();
    TagResolver fallback;
    if (tr->empty()) {
        fallback = fallback_tag_resolver(snapshot);
        tr = &fallback;
    }
    result.algo_input = CompactAdapter::apply(snapshot, request, *tr);
    result.target_eq_nsid = request.target_item.id;
    return result;
}

SolvePipeline::Stage2Result SolvePipeline::stage_execute(algorithm::AlgorithmInput& algo_input,
                                                         const std::string& algorithm,
                                                         algorithm::AlgorithmLoader& loader,
                                                         ActiveExecutorHandle* out_executor) {
    Stage2Result result;

    // The loader returns the algorithm domain's authoritative entry — an
    // IExecutor.  In sandbox mode this is a SandboxedExecutor (the real
    // executor runs inside a besq-worker subprocess); otherwise an in-process
    // AlgorithmExecutor.  Same contract either way.
    auto executor = loader.create_executor(algorithm);
    if (!executor) {
        auto available = loader.list();
        {
            std::string avail_str;
            for (size_t i = 0; i < available.size(); ++i) {
                if (i > 0)
                    avail_str += ", ";
                avail_str += available[i];
            }
            throw std::runtime_error(tr_fmt("pipeline.err.unknown_algo", algorithm, avail_str));
        }
    }

    // Check mode support
    if (!(executor->supported_mode() & algo_input.config.mode)) {
        std::string mode_str = (algo_input.config.mode == AlgorithmMode::inventory) ? "inventory" : "direct";
        throw std::runtime_error(tr_fmt("pipeline.err.unsupported_mode", algorithm, mode_str));
    }

    // Execute on the heap so a shared_ptr can publish the executor's address
    // while keeping it alive.  The handle only ever carries shared_ptr copies:
    // the stack-local `executor` keeps the object alive until this function
    // returns, and an abort thread that already copied the handle keeps it
    // alive even longer — so cancel() can never dereference a destroyed
    // executor.  The guard clears the handle on every exit path (including
    // exceptions), so the owning BesqContext never retains a stale reference.
    auto start = std::chrono::steady_clock::now();
    auto shared = std::shared_ptr<algorithm::IExecutor>(std::move(executor));
    ExecutorHandleGuard handle_guard{out_executor};
    if (out_executor)
        out_executor->store(shared);

    // Feasibility gate (cheap).  The resolver (which produces the strategy's
    // working item set) is called by the strategy itself inside execute().
    // Published above BEFORE this gate: a concurrent abort_solve() must find
    // the executor even while simulate()/start() have not run yet — a cancel
    // that lands on an Idle executor is recorded as pending and still aborts
    // the run (AlgorithmExecutor::cancel/_cancel_pending).
    result.algorithm_name = algorithm;
    if (!shared->simulate(algo_input)) {
        LOG_INFO("simulate: target not reachable");
        return result;
    }

    shared->start(algo_input);
    shared->wait();
    auto end = std::chrono::steady_clock::now();

    result.computation_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    result.algo_output = shared->output();
    return result;
}

SolveResult SolvePipeline::stage_recall(const algorithm::AlgorithmOutput& output, const algorithm::AlgorithmInput& algo_input) {
    SolveResult result;
    result.algorithm_used = output.algorithm_name;
    result.computation_time_ms = output.computation_time.count();

    result.solutions = CompactAdapter::recall(output, algo_input);
    result.success = !result.solutions.empty();
    return result;
}
