# ── Common domain tests ──────────────────────────────────────────────
# Tests for besq-common: I/O, utilities, config, types.

# --- Core type tests ---

add_executable(test_nsid
    test_nsid.cpp
    ${PROJECT_ROOT}/src/common/CommonTypes.cpp
)
target_include_directories(test_nsid PRIVATE ${TEST_INCLUDE})
add_test(NAME test_nsid COMMAND test_nsid)
set_tests_properties(test_nsid PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- I/O layer tests ---

add_executable(json_tests
    io/json_tests.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
)
target_include_directories(json_tests PRIVATE ${TEST_INCLUDE})
add_test(NAME json_tests COMMAND json_tests)
set_tests_properties(json_tests PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_byte_stream
    io/test_byte_stream.cpp
)
target_include_directories(test_byte_stream PRIVATE ${TEST_INCLUDE})
add_test(NAME test_byte_stream COMMAND test_byte_stream)
set_tests_properties(test_byte_stream PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

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

# Environment / Config
add_executable(test_env_util utils/test_env_util.cpp)
target_include_directories(test_env_util PRIVATE ${TEST_INCLUDE})
add_test(NAME test_env_util COMMAND test_env_util)
set_tests_properties(test_env_util PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_app_config config/test_app_config.cpp)
target_include_directories(test_app_config PRIVATE ${TEST_INCLUDE})
add_test(NAME test_app_config COMMAND test_app_config)
set_tests_properties(test_app_config PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- i18n tests ---

add_executable(test_i18n
    i18n/test_i18n.cpp
    ${PROJECT_ROOT}/src/common/i18n/Language.cpp
)

# --- CLI Parser v2 tests ---

add_executable(test_cli_parser_v2
    utils/test_cli_parser_v2.cpp
    ${PROJECT_ROOT}/src/common/utils/cli/CLIParser.cpp
    ${PROJECT_ROOT}/src/common/utils/cli/CLICommon.cpp
    ${PROJECT_ROOT}/src/common/utils/cli/CLIFormatter.cpp
)
target_include_directories(test_cli_parser_v2 PRIVATE ${TEST_INCLUDE})
add_test(NAME test_cli_parser_v2 COMMAND test_cli_parser_v2)
set_tests_properties(test_cli_parser_v2 PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})
target_include_directories(test_i18n PRIVATE ${TEST_INCLUDE})
add_test(NAME test_i18n COMMAND test_i18n)
set_tests_properties(test_i18n PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

