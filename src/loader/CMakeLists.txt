# Algorithm Loader source group
# Included from root CMakeLists.txt via include()

set(BESQ_LOADER_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/AlgorithmLoader.cpp"
)

# ── Plugin shared libraries (optional — -DBESQ_BUILD_PLUGINS=ON) ─────
# Each plugin is a standalone .so/.dll that AlgorithmLoader can load at
# runtime.  When enabled, besq-core is automatically built as a SHARED
# library so that host and plugins share the same heap + vtables.
#
# Usage:
#   cmake -B build -DBESQ_BUILD_PLUGINS=ON
#   cmake --build build
#   build/bin/besq --algo-dir build/bin/algorithms --list-algorithms

option(BESQ_BUILD_PLUGINS "Build algorithm plugins as shared libraries" OFF)

if(BESQ_BUILD_PLUGINS)
    # Helper: create a plugin shared library from a strategy class.
    function(add_algo_plugin NAME HEADER CLASS)
        set(PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/_plugins")
        set(PLUGIN_SRC "${PLUGIN_DIR}/${NAME}_plugin.cpp")
        file(WRITE "${PLUGIN_SRC}"
            "#include \"loader/PluginEntry.h\"\n"
            "#include \"${HEADER}\"\n"
            "BESQ_PLUGIN_ENTRY(${CLASS})\n"
        )
        add_library(algo_${NAME} SHARED "${PLUGIN_SRC}")
        target_include_directories(algo_${NAME} PRIVATE ${BESQ_PUBLIC_INCLUDES})
        target_link_libraries(algo_${NAME} PRIVATE besq-core)
        set_target_properties(algo_${NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/algorithms"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/algorithms"
        )
    endfunction()

    add_algo_plugin(greedy  "algorithm/strategies/greedy/GreedyAlgorithm.h"  GreedyAlgorithm)
    add_algo_plugin(dfs     "algorithm/strategies/dfs/DFSAlgorithm.h"        DFSAlgorithm)
    add_algo_plugin(astar   "algorithm/strategies/astar/AStarAlgorithm.h"    AStarAlgorithm)
    add_algo_plugin(idastar "algorithm/strategies/idastar/IDAStarAlgorithm.h" IDAStarAlgorithm)
    add_algo_plugin(hamming "algorithm/strategies/hamming/HammingAlgorithm.h" HammingAlgorithm)
    add_algo_plugin(hierarchical "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h" HierarchicalMergeAlgorithm)
    add_algo_plugin(penalty_balance "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h" DynamicPenaltyBalancingAlgorithm)
    add_algo_plugin(diff_first "algorithm/strategies/diff_first/DiffFirstAlgorithm.h" DiffFirstAlgorithm)
endif()
