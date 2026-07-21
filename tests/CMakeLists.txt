# --- Test targets for BestEnchSeq-Core ---
#
# Each test is a standalone executable with its own main() - no test framework.
#
# Tests that exercise project code link the besq-core library and only
# list the test source file.  Truly standalone tests (queues, pools,
# utility headers) stay independent with their own source lists.

# Project root (parent of tests/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path for all test targets
set(TEST_INCLUDE
    ${PROJECT_ROOT}/src
    ${BESQ_GENERATED_DIR}
)

# So #include "framework/test_utils.h" resolves
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# --- Helpers ---

# PATH extension for MinGW runtime DLLs and besq-core.dll on Windows
set(BESQ_TEST_ENV "PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(BESQ_RUNTIME_DLL_DIR AND EXISTS "${BESQ_RUNTIME_DLL_DIR}/libwinpthread-1.dll")
    set(BESQ_TEST_ENV "${BESQ_TEST_ENV}\\;${BESQ_RUNTIME_DLL_DIR}\\;$ENV{PATH}")
else()
    set(BESQ_TEST_ENV "${BESQ_TEST_ENV}\\;$ENV{PATH}")
endif()

function(besq_test_common_props NAME)
    add_test(NAME ${NAME} COMMAND ${NAME})
    if(BESQ_TEST_ENV)
        set_tests_properties(${NAME} PROPERTIES
            WORKING_DIRECTORY ${PROJECT_ROOT}
            ENVIRONMENT "${BESQ_TEST_ENV}"
        )
    else()
        set_tests_properties(${NAME} PROPERTIES
            WORKING_DIRECTORY ${PROJECT_ROOT}
        )
    endif()
endfunction()

# Default: link besq-core for all project-dependent tests.
function(besq_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${TEST_INCLUDE}
        ${PROJECT_ROOT}/include
    )
    target_link_libraries(${NAME} PRIVATE besq-core)
    besq_test_common_props(${NAME})
endfunction()

# For tests that also need CLI helper functions (parse_cli, build_target, ...)
function(besq_test_with_cli NAME SOURCE)
    add_executable(${NAME}
        ${SOURCE}
        ${PROJECT_ROOT}/src/cli/cli.cpp
        ${PROJECT_ROOT}/src/parsers/CLIParser.cpp
        ${PROJECT_ROOT}/src/parsers/EnchParser.cpp
        ${PROJECT_ROOT}/src/parsers/ItemParser.cpp
    )
    target_include_directories(${NAME} PRIVATE
        ${TEST_INCLUDE}
        ${PROJECT_ROOT}/include
    )
    target_link_libraries(${NAME} PRIVATE besq-core)
    besq_test_common_props(${NAME})
endfunction()

# Helper for standalone tests (with besq_test_common_props)
function(besq_standalone_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE ${TEST_INCLUDE})
    besq_test_common_props(${NAME})
endfunction()

# --- I/O layer tests ---

add_executable(json_tests
    io/json_tests.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(json_tests PRIVATE ${TEST_INCLUDE})
add_test(NAME json_tests COMMAND json_tests)
set_tests_properties(json_tests PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_byte_stream_reader
    io/test_byte_stream_reader.cpp
)
target_include_directories(test_byte_stream_reader PRIVATE ${TEST_INCLUDE})
add_test(NAME test_byte_stream_reader COMMAND test_byte_stream_reader)
set_tests_properties(test_byte_stream_reader PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- Parser tests ---

add_executable(test_ench_parser
    parsers/test_ench_parser.cpp
    ${PROJECT_ROOT}/src/parsers/EnchParser.cpp
    ${PROJECT_ROOT}/src/log/Logger.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(test_ench_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_ench_parser COMMAND test_ench_parser)
set_tests_properties(test_ench_parser PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_item_parser
    parsers/test_item_parser.cpp
    ${PROJECT_ROOT}/src/parsers/ItemParser.cpp
    ${PROJECT_ROOT}/src/parsers/EnchParser.cpp
    ${PROJECT_ROOT}/src/log/Logger.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(test_item_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_item_parser COMMAND test_item_parser)
set_tests_properties(test_item_parser PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

besq_test_with_cli(test_cli_acceptance cli/test_cli_acceptance.cpp)

# --- Adapter tests ---

besq_test(test_output_formatter adapters/test_output_formatter.cpp)
besq_test(test_compact_adapter adapters/test_compact_adapter.cpp)
besq_test(test_raw_type_adapter adapters/test_raw_type_adapter.cpp)
besq_test(test_enchantment_registry_lookup adapters/test_enchantment_registry_lookup.cpp)

# --- Resolver tests ---

besq_test_with_cli(test_item_resolver resolvers/test_item_resolver.cpp)
besq_test(test_inventory_resolver resolvers/test_inventory_resolver.cpp)
besq_test(test_tag_resolver utils/test_tag_resolver.cpp)

# --- Utility tests (standalone -- no project source dependencies) ---

# Queue tests
foreach(Q spsc spmc mpmc mpsc segmented_mpmc)
    add_executable(test_${Q}_queue utils/test_${Q}_queue.cpp)
    target_include_directories(test_${Q}_queue PRIVATE ${TEST_INCLUDE})
    add_test(NAME test_${Q}_queue COMMAND test_${Q}_queue)
    set_tests_properties(test_${Q}_queue PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})
endforeach()

# EventLoop
add_executable(test_event_loop utils/test_event_loop.cpp)
target_include_directories(test_event_loop PRIVATE ${TEST_INCLUDE})
add_test(NAME test_event_loop COMMAND test_event_loop)
set_tests_properties(test_event_loop PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# MemoryPool / ObjectPool
add_executable(test_memory_pool utils/MemoryPoolTest.cpp)
target_include_directories(test_memory_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_memory_pool COMMAND test_memory_pool)
set_tests_properties(test_memory_pool PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_object_pool utils/ObjectPoolTest.cpp)
target_include_directories(test_object_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_object_pool COMMAND test_object_pool)
set_tests_properties(test_object_pool PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# Environment / config
add_executable(test_env_util utils/test_env_util.cpp)
target_include_directories(test_env_util PRIVATE ${TEST_INCLUDE})
add_test(NAME test_env_util COMMAND test_env_util)
set_tests_properties(test_env_util PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_app_config config/test_app_config.cpp)
target_include_directories(test_app_config PRIVATE ${TEST_INCLUDE})
add_test(NAME test_app_config COMMAND test_app_config)
set_tests_properties(test_app_config PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# Generic parser utilities
add_executable(test_parser_utils
    utils/test_parser_utils.cpp
    ${PROJECT_ROOT}/src/log/Logger.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
)
target_include_directories(test_parser_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_parser_utils COMMAND test_parser_utils)
set_tests_properties(test_parser_utils PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- Type tests ---

besq_test(test_domain_types types/test_domain_types.cpp)
besq_test(test_exp_calculator utils/test_exp_calculator.cpp)

# --- Registry tests ---

besq_test(test_enchantment_registry registry/test_enchantment_registry.cpp)
besq_test(test_equipment_registry registry/test_equipment_registry.cpp)
besq_test(test_enchantment_registry_mutable registries/test_enchantment_registry_mutable.cpp)
besq_test(test_equipment_registry_mutable registries/test_equipment_registry_mutable.cpp)
besq_test(test_equipment_category_registry registry/test_equipment_category_registry.cpp)
besq_test(test_algorithm_registry registry/test_algorithm_registry.cpp)
besq_test(test_ench_reg registry/test_ench_reg.cpp)

# RegistryManager test needs its dedicated source (not in besq-core)
add_executable(test_registry_manager
    registries/test_registry_manager.cpp
    ${PROJECT_ROOT}/src/registries/RegistryManager.cpp
)
target_include_directories(test_registry_manager PRIVATE ${TEST_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(test_registry_manager PRIVATE besq-core)
add_test(NAME test_registry_manager COMMAND test_registry_manager)
set_tests_properties(test_registry_manager PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

# --- Algorithm layer tests ---

besq_test(test_forge_engine algorithm/test_forge_engine.cpp)
besq_test(test_execution_context algorithm/test_execution_context.cpp)
besq_test(test_algorithm_utils algorithm/test_algorithm_utils.cpp)
besq_test(test_astar_memory_budget algorithm/test_astar_memory_budget.cpp)
besq_test(test_algorithm_strategies algorithm/test_algorithm_strategies.cpp)
besq_test(test_algorithm_executor integration/test_algorithm_executor.cpp)

# Serialization tests
besq_test(test_compact_serializer algorithm/test_compact_serializer.cpp)
besq_test(test_astar_state_serializer algorithm/test_astar_state_serializer.cpp)

# --- Parser tests (full data loading) ---

besq_test(test_ench_info_parser parsers/test_ench_info_parser.cpp)

# --- Integration / acceptance tests ---

besq_test_with_cli(test_integration integration/test_integration.cpp)
besq_test_with_cli(test_algorithm_acceptance integration/test_algorithm_acceptance.cpp)
set_tests_properties(test_algorithm_acceptance PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
    TIMEOUT 120
)
besq_test(test_checkpoint_roundtrip integration/test_checkpoint_roundtrip.cpp)

# --- API tests ---

besq_test_with_cli(test_besq_context api/test_besq_context.cpp)
