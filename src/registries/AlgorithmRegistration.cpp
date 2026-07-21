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

#ifdef BESQ_HAVE_DFS
#  include "algorithm/strategies/dfs/DFSAlgorithm.h"
#endif
#ifdef BESQ_HAVE_ASTAR
#  include "algorithm/strategies/astar/AStarAlgorithm.h"
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
    // hamming — near-optimal quality, instant speed, minimal code.
    // Default everyday algorithm.
    registry.register_algorithm("hamming",
        [] { return std::make_unique<HammingAlgorithm>(); });

#ifdef BESQ_HAVE_DFS
    // dfs — simplest possible search, zero runtime overhead.
    // Lightweight fallback for resource-constrained environments.
    registry.register_algorithm("dfs",
        [] { return std::make_unique<DFSAlgorithm>(); });
#endif

#ifdef BESQ_HAVE_ASTAR
    // astar — optimal solver with search budget control.
    // Required when solution quality matters above all else.
    registry.register_algorithm("astar",
        [] { return std::make_unique<AStarAlgorithm>(); });
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
