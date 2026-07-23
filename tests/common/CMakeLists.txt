# ── Common domain tests ──────────────────────────────────────────────
# Tests for besq-common: I/O, utilities, config.

# --- I/O layer tests ---

add_executable(json_tests
    io/json_tests.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
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

# Generic parser utilities
add_executable(test_parser_utils
    utils/test_parser_utils.cpp
    ${PROJECT_ROOT}/src/common/log/Logger.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
    ${PROJECT_ROOT}/src/common/io/CsvIO.cpp
)
target_include_directories(test_parser_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_parser_utils COMMAND test_parser_utils)
set_tests_properties(test_parser_utils PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})
