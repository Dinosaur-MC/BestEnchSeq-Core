# ── Algorithm domain tests ──────────────────────────────────────────
# Tests for besq-domain-algorithm: compact types, strategies, forge engine, etc.

besq_test(test_forge_engine test_forge_engine.cpp)
besq_test(test_execution_context test_execution_context.cpp)
besq_test(test_algorithm_utils test_algorithm_utils.cpp)
besq_test(test_astar_memory_budget test_astar_memory_budget.cpp)
besq_test(test_ench_reg test_ench_reg.cpp)
besq_test(test_algorithm_registry test_algorithm_registry.cpp)

# Serialization tests (compact types, state serializers)
besq_test(test_compact_serializer test_compact_serializer.cpp)
besq_test(test_astar_state_serializer test_astar_state_serializer.cpp)
besq_test(test_dfs_state_serializer test_dfs_state_serializer.cpp)
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
besq_test(test_algorithm_strategies test_algorithm_strategies.cpp)
