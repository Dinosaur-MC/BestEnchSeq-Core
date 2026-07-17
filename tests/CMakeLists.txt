# ─── Test targets for BestEnchSeq-Core ───
# Paths are relative to CMAKE_CURRENT_SOURCE_DIR (i.e. <project_root>/tests/)

# Project root directory (parent of tests/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path for all test targets
set(TEST_INCLUDE
    ${PROJECT_ROOT}/src
    ${BESQ_GENERATED_DIR}
)

# Include tests/ so #include "framework/test_utils.h" resolves
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Match main target compile definitions (available via header fallbacks when absent)
add_compile_definitions(BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS})
if(BESQ_DISABLE_DIAGNOSTICS)
    add_compile_definitions(BESQ_DISABLE_DIAGNOSTICS)
endif()

# ============================================================================
# Shared dependency groups (reused across test targets)
# ============================================================================

# Type layer sources (from src/types/CMakeLists.txt)
include(${PROJECT_ROOT}/src/types/CMakeLists.txt)

# Core registries
set(REGISTRY_CORE_SRCS
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentCategoryRegistry.cpp
)

# Global dependencies
set(GLOBAL_LOGGER_SRCS ${PROJECT_ROOT}/src/log/Logger.cpp)

# Parser utilities
set(PARSER_UTIL_SRCS
    ${GLOBAL_LOGGER_SRCS}
    ${PROJECT_ROOT}/src/io/json.cpp
)

# Algorithm layer sources (from src/algorithm/CMakeLists.txt)
include(${PROJECT_ROOT}/src/algorithm/CMakeLists.txt)

# Algorithm serialization sources (from src/algorithm/serialization/CMakeLists.txt)
include(${PROJECT_ROOT}/src/algorithm/serialization/CMakeLists.txt)

# Compact registries (Logger linked here as CompactedRegistries depends on it)
set(COMPACT_REG_SRCS
    ${PROJECT_ROOT}/src/registries/CompactedRegistries.cpp
    ${GLOBAL_LOGGER_SRCS}
)

# Equipment registry
set(EQUIP_REG_SRCS ${PROJECT_ROOT}/src/registries/EquipmentRegistry.cpp)

# ============================================================================
# Existing tests
# ============================================================================

add_executable(json_tests
    io/json_tests.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(json_tests PRIVATE ${TEST_INCLUDE})
add_test(NAME json_tests COMMAND json_tests)
set_tests_properties(json_tests PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_parser_utils
    utils/test_parser_utils.cpp
    ${PARSER_UTIL_SRCS}
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
)
target_include_directories(test_parser_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_parser_utils COMMAND test_parser_utils)
set_tests_properties(test_parser_utils PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_tag_resolver
    utils/test_tag_resolver.cpp
    ${PARSER_UTIL_SRCS}
)
target_include_directories(test_tag_resolver PRIVATE ${TEST_INCLUDE})
add_test(NAME test_tag_resolver COMMAND test_tag_resolver)
set_tests_properties(test_tag_resolver PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_ench_info_parser
    parsers/test_ench_info_parser.cpp
    ${PROJECT_ROOT}/src/parsers/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/adapters/EnchSerializer.cpp
    ${PARSER_UTIL_SRCS}
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentCategoryRegistry.cpp
)
target_include_directories(test_ench_info_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_ench_info_parser COMMAND test_ench_info_parser)
set_tests_properties(test_ench_info_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_equipment_parser
    parsers/test_equipment_parser.cpp
    ${PROJECT_ROOT}/src/parsers/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/adapters/EnchSerializer.cpp
    ${PARSER_UTIL_SRCS}
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentCategoryRegistry.cpp
)
target_include_directories(test_equipment_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_equipment_parser COMMAND test_equipment_parser)
set_tests_properties(test_equipment_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_cli_parser
    parsers/test_cli_parser.cpp
    ${PROJECT_ROOT}/src/cli/cli.cpp
    ${PROJECT_ROOT}/src/parsers/CLIParser.cpp
    ${PARSER_UTIL_SRCS}
)
target_include_directories(test_cli_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_cli_parser COMMAND test_cli_parser)
set_tests_properties(test_cli_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_input_parser
    parsers/test_input_parser.cpp
    ${PROJECT_ROOT}/src/cli/cli.cpp
    ${PROJECT_ROOT}/src/parsers/InputParser.cpp
    ${PROJECT_ROOT}/src/adapters/RegistryResolver.cpp
    ${PROJECT_ROOT}/src/parsers/CLIParser.cpp
    ${PARSER_UTIL_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${DOMAIN_TYPE_SRCS}
)
target_include_directories(test_input_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_input_parser COMMAND test_input_parser)
set_tests_properties(test_input_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_output_formatter
    adapters/test_output_formatter.cpp
    ${PROJECT_ROOT}/src/adapters/OutputFormatter.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${REGISTRY_CORE_SRCS}
    ${TYPE_ALL_SRCS}
    ${EQUIP_REG_SRCS}
)
target_include_directories(test_output_formatter PRIVATE ${TEST_INCLUDE})
add_test(NAME test_output_formatter COMMAND test_output_formatter)
set_tests_properties(test_output_formatter PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_integration
    integration/test_integration.cpp
    # Parser layer
    ${PROJECT_ROOT}/src/cli/cli.cpp
    ${PROJECT_ROOT}/src/parsers/CLIParser.cpp
    ${PROJECT_ROOT}/src/parsers/InputParser.cpp
    ${PROJECT_ROOT}/src/parsers/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parsers/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/adapters/EnchSerializer.cpp
    ${PROJECT_ROOT}/src/adapters/RegistryResolver.cpp
    ${PROJECT_ROOT}/src/adapters/OutputFormatter.cpp
    # I/O layer
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    # Registry layer
    ${REGISTRY_CORE_SRCS}
    ${EQUIP_REG_SRCS}
    # Domain types
    ${TYPE_ALL_SRCS}
    # Algorithm layer (integration tests)
    ${ALGO_ENGINE_SRCS}
    ${ALGO_FORGE_SRCS}
    ${ALGO_DIAG_CORE_SRCS}
    ${ALGO_SERIAL_SRCS}
    ${COMPACT_REG_SRCS}
    ${PROJECT_ROOT}/src/adapters/CompactAdapter.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/greedy/GreedyAlgorithm.cpp
)
target_include_directories(test_integration PRIVATE ${TEST_INCLUDE})
add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

# ============================================================================
# New algorithm layer tests
# ============================================================================

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_forge_engine.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_forge_engine.cpp")
endif()

add_executable(test_forge_engine
    algorithm/test_forge_engine.cpp
    ${ALGO_FORGE_SRCS}
    ${COMPACT_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${TYPE_ALL_SRCS}
)
target_include_directories(test_forge_engine PRIVATE ${TEST_INCLUDE})
add_test(NAME test_forge_engine COMMAND test_forge_engine)
set_tests_properties(test_forge_engine PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_exp_calculator.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_exp_calculator.cpp")
endif()

add_executable(test_exp_calculator
    utils/test_exp_calculator.cpp
    ${DOMAIN_TYPE_SRCS}
    ${REGISTRY_CORE_SRCS}
)
target_include_directories(test_exp_calculator PRIVATE ${TEST_INCLUDE})
add_test(NAME test_exp_calculator COMMAND test_exp_calculator)
set_tests_properties(test_exp_calculator PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/types/test_domain_types.cpp)
    message(FATAL_ERROR "Required test source not found: types/test_domain_types.cpp")
endif()

add_executable(test_domain_types
    types/test_domain_types.cpp
    ${DOMAIN_TYPE_SRCS}
)
target_include_directories(test_domain_types PRIVATE ${TEST_INCLUDE})
add_test(NAME test_domain_types COMMAND test_domain_types)
set_tests_properties(test_domain_types PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_execution_context.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_execution_context.cpp")
endif()

add_executable(test_execution_context
    algorithm/test_execution_context.cpp
    ${PROJECT_ROOT}/src/algorithm/ExecutionContext.cpp
    ${ALGO_DIAG_CORE_SRCS}
    ${COMPACT_TYPE_SRCS}
    ${GLOBAL_LOGGER_SRCS}
)
target_include_directories(test_execution_context PRIVATE ${TEST_INCLUDE})
add_test(NAME test_execution_context COMMAND test_execution_context)
set_tests_properties(test_execution_context PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_algorithm_registry.cpp)
    message(FATAL_ERROR "Required test source not found: registry/test_algorithm_registry.cpp")
endif()

add_executable(test_algorithm_registry
    registry/test_algorithm_registry.cpp
    ${PROJECT_ROOT}/src/registries/AlgorithmRegistry.cpp
    ${REGISTRY_CORE_SRCS}
    ${DOMAIN_TYPE_SRCS}
)
target_include_directories(test_algorithm_registry PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_registry COMMAND test_algorithm_registry)
set_tests_properties(test_algorithm_registry PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_algorithm_executor.cpp)
    message(FATAL_ERROR "Required test source not found: integration/test_algorithm_executor.cpp")
endif()

add_executable(test_algorithm_executor
    integration/test_algorithm_executor.cpp
    ${ALGO_ENGINE_SRCS}
    ${ALGO_FORGE_SRCS}
    ${ALGO_DIAG_CORE_SRCS}
    ${ALGO_SERIAL_SRCS}
    ${COMPACT_REG_SRCS}
    ${TYPE_ALL_SRCS}
    ${PROJECT_ROOT}/src/registries/AlgorithmRegistry.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
)
target_include_directories(test_algorithm_executor PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_executor COMMAND test_algorithm_executor)
set_tests_properties(test_algorithm_executor PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_algorithm_strategies.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_algorithm_strategies.cpp")
endif()

# ALGO_STRATEGY_SRGS and other ALGO_* variables already loaded
# from src/algorithm/CMakeLists.txt above.

add_executable(test_algorithm_strategies
    algorithm/test_algorithm_strategies.cpp
    ${ALGO_ENGINE_SRCS}
    ${ALGO_FORGE_SRCS}
    ${ALGO_DIAG_CORE_SRCS}
    ${ALGO_STRATEGY_SRGS}
    ${PROJECT_ROOT}/src/algorithm/strategies/astar/AStarMemoryBudget.cpp
    ${TYPE_ALL_SRCS}
    ${COMPACT_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${PROJECT_ROOT}/src/registries/AlgorithmRegistry.cpp
    ${ALGO_SERIAL_SRCS}
)
target_include_directories(test_algorithm_strategies PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_strategies COMMAND test_algorithm_strategies)
set_tests_properties(test_algorithm_strategies PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_spmc_queue.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_spmc_queue.cpp")
endif()

add_executable(test_spmc_queue
    utils/test_spmc_queue.cpp
)
target_include_directories(test_spmc_queue PRIVATE ${TEST_INCLUDE})
add_test(NAME test_spmc_queue COMMAND test_spmc_queue)
set_tests_properties(test_spmc_queue PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_spsc_queue.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_spsc_queue.cpp")
endif()

add_executable(test_spsc_queue
    utils/test_spsc_queue.cpp
)
target_include_directories(test_spsc_queue PRIVATE ${TEST_INCLUDE})
add_test(NAME test_spsc_queue COMMAND test_spsc_queue)
set_tests_properties(test_spsc_queue PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_algorithm_utils.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_algorithm_utils.cpp")
endif()

add_executable(test_algorithm_utils
    algorithm/test_algorithm_utils.cpp
)
target_include_directories(test_algorithm_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_utils COMMAND test_algorithm_utils)
set_tests_properties(test_algorithm_utils PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_ench_reg.cpp)
    message(FATAL_ERROR "Required test source not found: registry/test_ench_reg.cpp")
endif()

add_executable(test_ench_reg
    registry/test_ench_reg.cpp
    ${COMPACT_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${TYPE_ALL_SRCS}
)
target_include_directories(test_ench_reg PRIVATE ${TEST_INCLUDE})
add_test(NAME test_ench_reg COMMAND test_ench_reg)
set_tests_properties(test_ench_reg PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_equipment_category_registry.cpp)
    message(FATAL_ERROR "Required test source not found: registry/test_equipment_category_registry.cpp")
endif()

add_executable(test_equipment_category_registry
    registry/test_equipment_category_registry.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentCategoryRegistry.cpp
)
target_include_directories(test_equipment_category_registry PRIVATE ${TEST_INCLUDE})
add_test(NAME test_equipment_category_registry COMMAND test_equipment_category_registry)
set_tests_properties(test_equipment_category_registry PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_astar_memory_budget.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_astar_memory_budget.cpp")
endif()

add_executable(test_astar_memory_budget
    algorithm/test_astar_memory_budget.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/astar/AStarMemoryBudget.cpp
)
target_include_directories(test_astar_memory_budget PRIVATE ${TEST_INCLUDE})
add_test(NAME test_astar_memory_budget COMMAND test_astar_memory_budget)
set_tests_properties(test_astar_memory_budget PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_compact_adapter.cpp)
    message(FATAL_ERROR "Required test source not found: adapters/test_compact_adapter.cpp")
endif()

add_executable(test_compact_adapter
    adapters/test_compact_adapter.cpp
    ${PROJECT_ROOT}/src/adapters/CompactAdapter.cpp
    ${COMPACT_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${EQUIP_REG_SRCS}
    ${TYPE_ALL_SRCS}
)
target_include_directories(test_compact_adapter PRIVATE ${TEST_INCLUDE})
add_test(NAME test_compact_adapter COMMAND test_compact_adapter)
set_tests_properties(test_compact_adapter PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_registry_resolver.cpp)
    message(FATAL_ERROR "Required test source not found: adapters/test_registry_resolver.cpp")
endif()

add_executable(test_registry_resolver
    adapters/test_registry_resolver.cpp
    ${PROJECT_ROOT}/src/adapters/RegistryResolver.cpp
    ${REGISTRY_CORE_SRCS}
)
target_include_directories(test_registry_resolver PRIVATE ${TEST_INCLUDE})
add_test(NAME test_registry_resolver COMMAND test_registry_resolver)
set_tests_properties(test_registry_resolver PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_env_util.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_env_util.cpp")
endif()

add_executable(test_env_util
    utils/test_env_util.cpp
)
target_include_directories(test_env_util PRIVATE ${TEST_INCLUDE})
add_test(NAME test_env_util COMMAND test_env_util)
set_tests_properties(test_env_util PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/MemoryPoolTest.cpp)
    message(FATAL_ERROR "Required test source not found: utils/MemoryPoolTest.cpp")
endif()

add_executable(test_memory_pool
    utils/MemoryPoolTest.cpp
)
target_include_directories(test_memory_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_memory_pool COMMAND test_memory_pool)
set_tests_properties(test_memory_pool PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/ObjectPoolTest.cpp)
    message(FATAL_ERROR "Required test source not found: utils/ObjectPoolTest.cpp")
endif()

add_executable(test_object_pool
    utils/ObjectPoolTest.cpp
)
target_include_directories(test_object_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_object_pool COMMAND test_object_pool)
set_tests_properties(test_object_pool PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/test_app_config.cpp)
    message(FATAL_ERROR "Required test source not found: config/test_app_config.cpp")
endif()

add_executable(test_app_config
    config/test_app_config.cpp
)
target_include_directories(test_app_config PRIVATE ${TEST_INCLUDE})
add_test(NAME test_app_config COMMAND test_app_config)
set_tests_properties(test_app_config PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_mpmc_queue.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_mpmc_queue.cpp")
endif()

add_executable(test_mpmc_queue
    utils/test_mpmc_queue.cpp
)
target_include_directories(test_mpmc_queue PRIVATE ${TEST_INCLUDE})
add_test(NAME test_mpmc_queue COMMAND test_mpmc_queue)
set_tests_properties(test_mpmc_queue PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_segmented_mpmc_queue.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_segmented_mpmc_queue.cpp")
endif()

add_executable(test_segmented_mpmc_queue
    utils/test_segmented_mpmc_queue.cpp
)
target_include_directories(test_segmented_mpmc_queue PRIVATE ${TEST_INCLUDE})
add_test(NAME test_segmented_mpmc_queue COMMAND test_segmented_mpmc_queue)
set_tests_properties(test_segmented_mpmc_queue PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/utils/test_event_loop.cpp)
    message(FATAL_ERROR "Required test source not found: utils/test_event_loop.cpp")
endif()

add_executable(test_event_loop
    utils/test_event_loop.cpp
)
target_include_directories(test_event_loop PRIVATE ${TEST_INCLUDE})
add_test(NAME test_event_loop COMMAND test_event_loop)
set_tests_properties(test_event_loop PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_enchantment_registry.cpp)
    message(FATAL_ERROR "Required test source not found: registry/test_enchantment_registry.cpp")
endif()

add_executable(test_enchantment_registry
    registry/test_enchantment_registry.cpp
    ${REGISTRY_CORE_SRCS}
    ${DOMAIN_TYPE_SRCS}
)
target_include_directories(test_enchantment_registry PRIVATE ${TEST_INCLUDE})
add_test(NAME test_enchantment_registry COMMAND test_enchantment_registry)
set_tests_properties(test_enchantment_registry PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_equipment_registry.cpp)
    message(FATAL_ERROR "Required test source not found: registry/test_equipment_registry.cpp")
endif()

add_executable(test_equipment_registry
    registry/test_equipment_registry.cpp
    ${EQUIP_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
    ${DOMAIN_TYPE_SRCS}
)
target_include_directories(test_equipment_registry PRIVATE ${TEST_INCLUDE})
add_test(NAME test_equipment_registry COMMAND test_equipment_registry)
set_tests_properties(test_equipment_registry PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

# ============================================================================
# Algorithm serialization tests
# ============================================================================

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_compact_serializer.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_compact_serializer.cpp")
endif()

add_executable(test_compact_serializer
    algorithm/test_compact_serializer.cpp
    ${ALGO_SERIAL_SRCS}
    ${COMPACT_TYPE_SRCS}
)
target_include_directories(test_compact_serializer PRIVATE ${TEST_INCLUDE})
add_test(NAME test_compact_serializer COMMAND test_compact_serializer)
set_tests_properties(test_compact_serializer PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/test_astar_state_serializer.cpp)
    message(FATAL_ERROR "Required test source not found: algorithm/test_astar_state_serializer.cpp")
endif()

add_executable(test_astar_state_serializer
    algorithm/test_astar_state_serializer.cpp
    ${ALGO_ALL_SRCS}
    ${ALGO_SERIAL_SRCS}
    ${COMPACT_TYPE_SRCS}
    ${COMPACT_REG_SRCS}
    ${REGISTRY_CORE_SRCS}
)
target_include_directories(test_astar_state_serializer PRIVATE ${TEST_INCLUDE})
add_test(NAME test_astar_state_serializer COMMAND test_astar_state_serializer)
set_tests_properties(test_astar_state_serializer PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
