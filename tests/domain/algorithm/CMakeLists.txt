# ── Algorithm domain tests ──────────────────────────────────────────
# Tests for besq-domain-algorithm: compact types, strategies, forge engine, etc.

# astar / dfs 已从内置迁为 plugins/ 下的插件算法。沿用 test_penalty_balance
# 模式：插件算法源码是普通 IAlgorithm，直接编译进 host 单元测试（而非依赖
# 插件 .dll），需要把 plugins/ 放进 include 路径。
set(BESQ_PLUGIN_ASTAR_SRCS
    ${PROJECT_ROOT}/plugins/astar/AStarAlgorithm.cpp
    ${PROJECT_ROOT}/plugins/astar/AStarDiagnostics.cpp
    ${PROJECT_ROOT}/plugins/astar/AStarMemoryBudget.cpp
    ${PROJECT_ROOT}/plugins/astar/AStarStateSerializer.cpp)
set(BESQ_PLUGIN_DFS_SRCS
    ${PROJECT_ROOT}/plugins/dfs/DFSAlgorithm.cpp
    ${PROJECT_ROOT}/plugins/dfs/DFSStateSerializer.cpp)

# 把插件源码编译进 host 测试可执行文件（penalty_balance 同款模式）。
# PLUGIN_SRCS 需以引号传列表（如 "${BESQ_PLUGIN_ASTAR_SRCS}"）。
function(besq_test_with_plugins NAME SOURCE PLUGIN_SRCS)
    add_executable(${NAME} ${SOURCE} ${PLUGIN_SRCS})
    target_include_directories(${NAME} PRIVATE
        ${TEST_INCLUDE}
        ${PROJECT_ROOT}/include
        ${PROJECT_ROOT}/plugins
    )
    target_link_libraries(${NAME} PRIVATE besq-core)
    besq_test_common_props(${NAME})
endfunction()

besq_test(test_forge_engine test_forge_engine.cpp)
besq_test(test_execution_context test_execution_context.cpp)
besq_test(test_algorithm_utils test_algorithm_utils.cpp)
besq_test(test_ench_reg test_ench_reg.cpp)
besq_test(test_algorithm_registry test_algorithm_registry.cpp)

# astar memory budget: AStarMemoryBudget 已随 astar 迁出 besq-core，测试需编译插件源码
besq_test_with_plugins(test_astar_memory_budget test_astar_memory_budget.cpp
    ${PROJECT_ROOT}/plugins/astar/AStarMemoryBudget.cpp)

# Serialization tests (compact types, state serializers)
besq_test(test_compact_serializer test_compact_serializer.cpp)

# astar/dfs 序列化测试：编译插件源码（astar 的跨算法测试还需要 DFSAlgorithm 类型）
besq_test_with_plugins(test_astar_state_serializer test_astar_state_serializer.cpp
    "${BESQ_PLUGIN_ASTAR_SRCS};${BESQ_PLUGIN_DFS_SRCS}")
besq_test_with_plugins(test_dfs_state_serializer test_dfs_state_serializer.cpp
    "${BESQ_PLUGIN_DFS_SRCS}")
besq_test(test_dp_merge_state_serializer test_dp_merge_state_serializer.cpp)

besq_test(test_diagnostics test_diagnostics.cpp)

# penalty_balance plugin algorithm: the plugin is a separate shared-lib pipeline,
# but its algorithm source is a plain IAlgorithm and links directly into a unit
# test.  Compiled from plugins/ so it needs that tree on the include path.
add_executable(test_penalty_balance test_penalty_balance.cpp
    ${PROJECT_ROOT}/plugins/penalty_balance/DynamicPenaltyBalancingAlgorithm.cpp)
target_include_directories(test_penalty_balance PRIVATE
    ${TEST_INCLUDE}
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/plugins
)
target_link_libraries(test_penalty_balance PRIVATE besq-core)
besq_test_common_props(test_penalty_balance)

besq_test(test_algorithm_loader test_algorithm_loader.cpp)
besq_test(test_plugin_audit test_plugin_audit.cpp)

# astar/dfs 已为插件：策略正确性测试直接编译插件源码（保留 bb_dp 以 A* 为最优 oracle）
besq_test_with_plugins(test_algorithm_strategies test_algorithm_strategies.cpp
    "${BESQ_PLUGIN_ASTAR_SRCS};${BESQ_PLUGIN_DFS_SRCS}")
