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

add_executable(test_json
    io/test_json.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
)
target_include_directories(test_json PRIVATE ${TEST_INCLUDE})
add_test(NAME test_json COMMAND test_json)
set_tests_properties(test_json PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_byte_stream
    io/test_byte_stream.cpp
)
target_include_directories(test_byte_stream PRIVATE ${TEST_INCLUDE})
add_test(NAME test_byte_stream COMMAND test_byte_stream)
set_tests_properties(test_byte_stream PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# CSV I/O (compiles CsvIO.cpp directly, mirroring test_json)
add_executable(test_csv_io
    io/test_csv_io.cpp
    ${PROJECT_ROOT}/src/common/io/CsvIO.cpp
)
target_include_directories(test_csv_io PRIVATE ${TEST_INCLUDE})
add_test(NAME test_csv_io COMMAND test_csv_io)
set_tests_properties(test_csv_io PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# FileUtils (header-only; list_directory for the web directory picker)
add_executable(test_file_utils
    io/test_file_utils.cpp
)
target_include_directories(test_file_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_file_utils COMMAND test_file_utils)
set_tests_properties(test_file_utils PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# StringUtils (header-only)
add_executable(test_string_utils
    utils/test_string_utils.cpp
)
target_include_directories(test_string_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_string_utils COMMAND test_string_utils)
set_tests_properties(test_string_utils PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

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

# ThreadPool
add_executable(test_thread_pool
    utils/test_thread_pool.cpp
    ${PROJECT_ROOT}/src/common/utils/thread/ThreadPool.cpp
)
target_include_directories(test_thread_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_thread_pool COMMAND test_thread_pool)
set_tests_properties(test_thread_pool PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# MemoryPool / ObjectPool
add_executable(test_memory_pool utils/test_memory_pool.cpp)
target_include_directories(test_memory_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_memory_pool COMMAND test_memory_pool)
set_tests_properties(test_memory_pool PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_object_pool utils/test_object_pool.cpp)
target_include_directories(test_object_pool PRIVATE ${TEST_INCLUDE})
add_test(NAME test_object_pool COMMAND test_object_pool)
set_tests_properties(test_object_pool PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# bit_iterator (header-only utility)
add_executable(test_bit_iterator utils/test_bit_iterator.cpp)
target_include_directories(test_bit_iterator PRIVATE ${TEST_INCLUDE})
add_test(NAME test_bit_iterator COMMAND test_bit_iterator)
set_tests_properties(test_bit_iterator PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# Environment / Config
add_executable(test_env_util utils/test_env_util.cpp)
target_include_directories(test_env_util PRIVATE ${TEST_INCLUDE})
add_test(NAME test_env_util COMMAND test_env_util)
set_tests_properties(test_env_util PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# exe_dir() (header-only; basis for all runtime default paths)
add_executable(test_exe_dir utils/test_exe_dir.cpp)
target_include_directories(test_exe_dir PRIVATE ${TEST_INCLUDE})
add_test(NAME test_exe_dir COMMAND test_exe_dir)
set_tests_properties(test_exe_dir PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# ExpCalculator (needs business types -> besq-core link)
add_executable(test_exp_calculator utils/test_exp_calculator.cpp)
target_include_directories(test_exp_calculator PRIVATE ${TEST_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(test_exp_calculator PRIVATE besq-core)
add_test(NAME test_exp_calculator COMMAND test_exp_calculator)
set_tests_properties(test_exp_calculator PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# AppConfig (config.json persistence needs Json + Logger → besq-core link)
add_executable(test_app_config config/test_app_config.cpp)
target_include_directories(test_app_config PRIVATE ${TEST_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(test_app_config PRIVATE besq-core)
add_test(NAME test_app_config COMMAND test_app_config)
set_tests_properties(test_app_config PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- i18n tests ---

add_executable(test_i18n
    i18n/test_i18n.cpp
    ${PROJECT_ROOT}/src/common/i18n/Language.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
)

# --- CLI Parser v2 tests ---

add_executable(test_cli_parser_v2
    utils/test_cli_parser_v2.cpp
    ${PROJECT_ROOT}/src/common/utils/cli/CLICommon.cpp
    ${PROJECT_ROOT}/src/common/utils/cli/CLIFormatter.cpp
)
target_include_directories(test_cli_parser_v2 PRIVATE ${TEST_INCLUDE})
add_test(NAME test_cli_parser_v2 COMMAND test_cli_parser_v2)
set_tests_properties(test_cli_parser_v2 PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})
target_include_directories(test_i18n PRIVATE ${TEST_INCLUDE})
add_test(NAME test_i18n COMMAND test_i18n)
set_tests_properties(test_i18n PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- DataSchema engine ---

add_executable(test_data_schema
    ds/test_data_schema.cpp
)
target_include_directories(test_data_schema PRIVATE ${TEST_INCLUDE})
target_link_libraries(test_data_schema PRIVATE besq-core)
add_test(NAME test_data_schema COMMAND test_data_schema)
set_tests_properties(test_data_schema PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

# --- Logger (links the shared besq-common-log; DLL resolved via BESQ_TEST_ENV PATH) ---

add_executable(test_logger
    log/test_logger.cpp
)
target_include_directories(test_logger PRIVATE ${TEST_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(test_logger PRIVATE besq-common-log)
besq_test_common_props(test_logger)

