# ── Interface domain tests ──────────────────────────────────────────
# Tests for besq-domain-interface: CLI, parsers, API.

# Parser tests (standalone — compile their own sources)
add_executable(test_ench_parser
    test_ench_parser.cpp
    ${PROJECT_ROOT}/src/domain/interface/parsers/EnchParser.cpp
    ${PROJECT_ROOT}/src/common/log/Logger.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
)
target_include_directories(test_ench_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_ench_parser COMMAND test_ench_parser)
set_tests_properties(test_ench_parser PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

add_executable(test_item_parser
    test_item_parser.cpp
    ${PROJECT_ROOT}/src/domain/interface/parsers/ItemParser.cpp
    ${PROJECT_ROOT}/src/domain/interface/parsers/EnchParser.cpp
    ${PROJECT_ROOT}/src/common/log/Logger.cpp
    ${PROJECT_ROOT}/src/common/io/json.cpp
)
target_include_directories(test_item_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_item_parser COMMAND test_item_parser)
set_tests_properties(test_item_parser PROPERTIES WORKING_DIRECTORY ${PROJECT_ROOT})

besq_test(test_cli_parser test_cli_parser.cpp)
besq_test(test_cli_acceptance test_cli_acceptance.cpp)

# Full data loading test
besq_test(test_ench_info_parser test_ench_info_parser.cpp)

# API context test (needs BesqContext compiled — enabled now)
# TODO: currently has linker errors; re-enable when API layer is complete
# besq_test(test_besq_context test_besq_context.cpp)
