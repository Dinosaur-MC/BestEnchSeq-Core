#pragma once

/// @file tests/framework/TestLoader.h
/// Shared AlgorithmLoader fixture for tests and benchmarks.
///
/// Provides a fully-populated AlgorithmLoader with both built-in and
/// plugin-only strategies registered, so callers don't need to know
/// which strategies are compiled in vs plugin-delivered.

#include "loader/AlgorithmLoader.h"
#include "registries/AlgorithmRegistry.h"

#include <memory>
#include <mutex>

// Plugin-only strategies — compiled into besq-core but normally only
// registered via plugins.  We register them here for testing so that
// tests and benchmarks can create any strategy by name.
#ifdef BESQ_HAVE_GREEDY
#include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#endif
#ifdef BESQ_HAVE_IDASTAR
#include "algorithm/strategies/idastar/IDAStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
#include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
#include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
#include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#endif

/// Returns a process-wide AlgorithmLoader with all strategies registered
/// (built-in + plugin-only), safe to call multiple times.
inline AlgorithmLoader& test_loader() {
    static AlgorithmLoader loader;
    static std::once_flag flag;
    std::call_once(flag, [] {
        loader.load_builtin();

        // Register plugin-only strategies that are compiled into besq-core
#ifdef BESQ_HAVE_GREEDY
        loader.register_algorithm("greedy",
            [] { return std::make_unique<GreedyAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_IDASTAR
        loader.register_algorithm("idastar",
            [] { return std::make_unique<IDAStarAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
        loader.register_algorithm("hierarchical",
            [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
        loader.register_algorithm("penalty_balance",
            [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
        loader.register_algorithm("diff_first",
            [] { return std::make_unique<DiffFirstAlgorithm>(); });
        // "difficulty_first" is an alias
        loader.register_algorithm("difficulty_first",
            [] { return std::make_unique<DiffFirstAlgorithm>(); });
#endif
    });
    return loader;
}
