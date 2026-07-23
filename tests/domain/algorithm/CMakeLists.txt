# ── Algorithm domain tests ──────────────────────────────────────────
# Tests for besq-domain-algorithm: compact types, strategies, forge engine, etc.

besq_test(test_forge_engine test_forge_engine.cpp)
besq_test(test_execution_context test_execution_context.cpp)
besq_test(test_algorithm_utils test_algorithm_utils.cpp)
besq_test(test_astar_memory_budget test_astar_memory_budget.cpp)
besq_test(test_ench_reg test_ench_reg.cpp)
besq_test(test_algorithm_registry test_algorithm_registry.cpp)

# Serialization tests (compact types, AStar state serializer)
# TODO: fix compact_serializer — needs to use compact types instead of business types
# besq_test(test_compact_serializer test_compact_serializer.cpp)
besq_test(test_astar_state_serializer test_astar_state_serializer.cpp)

# Strategy integration tests (needs registries, AlgorithmExecutor, RegistryAccess)
# TODO: re-enable when CompactAdapter / AlgorithmExecutor APIs stabilize
# besq_test(test_algorithm_strategies test_algorithm_strategies.cpp)
