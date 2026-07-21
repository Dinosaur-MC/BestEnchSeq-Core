# Strategy source groups — each strategy is individually selectable.
# Included from algorithm/CMakeLists.txt.
#
# Built-in strategies: compiled into besq-core (always available)
# Plugin strategies:   only available as runtime-loaded .so/.dll

# ── Built-in ──────────────────────────────────────────────────────────
set(BESQ_STRATEGY_HAMMING_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/hamming/HammingAlgorithm.cpp"
)
set(BESQ_STRATEGY_DFS_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/dfs/DFSAlgorithm.cpp"
)
set(BESQ_STRATEGY_ASTAR_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/astar/AStarAlgorithm.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/astar/AStarDiagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/astar/AStarMemoryBudget.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/astar/AStarStateSerializer.cpp"
)

# ── Plugin-only ──────────────────────────────────────────────────────
set(BESQ_STRATEGY_GREEDY_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/greedy/GreedyAlgorithm.cpp"
)
set(BESQ_STRATEGY_IDASTAR_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/idastar/IDAStarAlgorithm.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/idastar/IDAStarDiagnostics.cpp"
)
set(BESQ_STRATEGY_HIERARCHICAL_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/hierarchical/HierarchicalMergeAlgorithm.cpp"
)
set(BESQ_STRATEGY_PENALTY_BALANCE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/penalty_balance/DynamicPenaltyBalancingAlgorithm.cpp"
)
set(BESQ_STRATEGY_DIFF_FIRST_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/diff_first/DiffFirstAlgorithm.cpp"
)

# Convenience: built-in strategies (what besq-core links)
set(BESQ_BUILTIN_STRATEGY_SRCS
    ${BESQ_STRATEGY_HAMMING_SRCS}
    ${BESQ_STRATEGY_DFS_SRCS}
    ${BESQ_STRATEGY_ASTAR_SRCS}
)

# Convenience: all strategies (for plugin builds)
set(BESQ_ALL_STRATEGY_SRCS
    ${BESQ_BUILTIN_STRATEGY_SRCS}
    ${BESQ_STRATEGY_DFS_SRCS}
    ${BESQ_STRATEGY_IDASTAR_SRCS}
    ${BESQ_STRATEGY_HIERARCHICAL_SRCS}
    ${BESQ_STRATEGY_PENALTY_BALANCE_SRCS}
    ${BESQ_STRATEGY_DIFF_FIRST_SRCS}
)
