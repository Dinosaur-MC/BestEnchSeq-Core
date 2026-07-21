// =============================================================================
// Algorithm Registration
//
// Maintains the process-wide AlgorithmRegistry singleton with all compiled-in
// strategy factories.  Plugin-loadable strategies (via PluginLoader) extend
// this registry at runtime.
//
// Each strategy is guarded by a BESQ_HAVE_* compile-time define so that
// minimal builds register only what they link.
// =============================================================================

#include "registries/AlgorithmRegistration.h"
#include "registries/AlgorithmRegistry.h"
#include "algorithm/strategies/hamming/HammingAlgorithm.h"
#include "log/log.hpp"

#ifdef BESQ_HAVE_GREEDY
#  include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DFS
#  include "algorithm/strategies/dfs/DFSAlgorithm.h"
#endif
#ifdef BESQ_HAVE_ASTAR
#  include "algorithm/strategies/astar/AStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_IDASTAR
#  include "algorithm/strategies/idastar/IDAStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
#  include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
#  include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
#  include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#endif

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// ====================================================================
// Registration
// ====================================================================

void register_builtin_algorithms(AlgorithmRegistry& registry) {
    registry.register_algorithm("hamming",
        [] { return std::make_unique<HammingAlgorithm>(); });

#ifdef BESQ_HAVE_GREEDY
    registry.register_algorithm("greedy",
        [] { return std::make_unique<GreedyAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_DFS
    registry.register_algorithm("dfs",
        [] { return std::make_unique<DFSAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_ASTAR
    registry.register_algorithm("astar",
        [] { return std::make_unique<AStarAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_IDASTAR
    registry.register_algorithm("idastar",
        [] { return std::make_unique<IDAStarAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
    registry.register_algorithm("hierarchical",
        [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
    registry.register_algorithm("penalty_balance",
        [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
    registry.register_algorithm("difficulty_first",
        [] { return std::make_unique<DiffFirstAlgorithm>(); });
    registry.register_algorithm("diff_first",
        [] { return std::make_unique<DiffFirstAlgorithm>(); });
#endif
}

// ====================================================================
// Global registry singleton
// ====================================================================

AlgorithmRegistry& global_algorithm_registry() {
    static AlgorithmRegistry reg;
    static std::once_flag flag;
    std::call_once(flag, [&] {
        register_builtin_algorithms(reg);
        LOG_INFO("Initialised global algorithm registry with %zu built-in "
                 "strategy/ies", reg.size());
    });
    return reg;
}

// ====================================================================
// Built-in factory (standalone, no global registry dependency)
// ====================================================================

std::unique_ptr<IAlgorithm> create_builtin_algorithm(const std::string& name) {
    AlgorithmRegistry reg;
    register_builtin_algorithms(reg);
    auto algo = reg.create(name);
    if (!algo) {
        auto available = reg.list();
        std::string msg = "Unknown algorithm: '" + name + "'. Available: ";
        for (size_t i = 0; i < available.size(); ++i) {
            if (i > 0) msg += ", ";
            msg += available[i];
        }
        throw std::runtime_error(msg);
    }
    return algo;
}
