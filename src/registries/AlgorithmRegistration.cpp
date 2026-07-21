// =============================================================================
// Algorithm Registration
//
// Registers built-in algorithm strategies with the AlgorithmRegistry.
// Each strategy is guarded by a compile-time define so that minimal builds
// can include only the strategies they need.
//
// Defines:
//   BESQ_HAVE_GREEDY          — GreedyAlgorithm
//   BESQ_HAVE_DFS             — DFSAlgorithm
//   BESQ_HAVE_ASTAR           — AStarAlgorithm (+ AStarMemoryBudget)
//   BESQ_HAVE_IDASTAR         — IDAStarAlgorithm
//   BESQ_HAVE_HAMMING         — HammingAlgorithm (always present in practice)
//   BESQ_HAVE_HIERARCHICAL    — HierarchicalMergeAlgorithm
//   BESQ_HAVE_PENALTY_BALANCE — DynamicPenaltyBalancingAlgorithm
//   BESQ_HAVE_DIFF_FIRST      — DiffFirstAlgorithm
//
// Consumers call detail::create_algorithm(name) to instantiate from the
// built-in registry.  This replaces the old approach of inlining all
// registrations inside SolvePipeline.cpp.
// =============================================================================

#include "registries/AlgorithmRegistration.h"
#include "registries/AlgorithmRegistry.h"
#include "algorithm/strategies/hamming/HammingAlgorithm.h"

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
#include <stdexcept>
#include <string>
#include <vector>

// ====================================================================
// Registration
// ====================================================================

/// Register all built-in (compiled-in) algorithm strategies.
/// Called once per create_algorithm() call to build a fresh registry.
static void register_builtin_algorithms(AlgorithmRegistry& registry) {
    // hamming is always present — the minimal default
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
// Factory
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
