# ─── Test targets for BestEnchSeq-Core ───
# Paths are relative to CMAKE_CURRENT_SOURCE_DIR (i.e. <project_root>/tests/)

# Project root directory (parent of tests/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path for all test targets
set(TEST_INCLUDE ${PROJECT_ROOT}/src)

# ============================================================================
# Existing tests
# ============================================================================

add_executable(json_tests
    json_tests.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(json_tests PRIVATE ${TEST_INCLUDE})
add_test(NAME json_tests COMMAND json_tests)
set_tests_properties(json_tests PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_parser_utils
    test_parser_utils.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
)
target_include_directories(test_parser_utils PRIVATE ${TEST_INCLUDE})
add_test(NAME test_parser_utils COMMAND test_parser_utils)
set_tests_properties(test_parser_utils PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_tag_resolver
    test_tag_resolver.cpp
    ${PROJECT_ROOT}/src/parser/TagResolver.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(test_tag_resolver PRIVATE ${TEST_INCLUDE})
add_test(NAME test_tag_resolver COMMAND test_tag_resolver)
set_tests_properties(test_tag_resolver PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_ench_info_parser
    test_ench_info_parser.cpp
    ${PROJECT_ROOT}/src/parser/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parser/TagResolver.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
)
target_include_directories(test_ench_info_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_ench_info_parser COMMAND test_ench_info_parser)
set_tests_properties(test_ench_info_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_equipment_parser
    test_equipment_parser.cpp
    ${PROJECT_ROOT}/src/parser/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
)
target_include_directories(test_equipment_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_equipment_parser COMMAND test_equipment_parser)
set_tests_properties(test_equipment_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_cli_parser
    test_cli_parser.cpp
    ${PROJECT_ROOT}/src/parser/CLIParser.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
)
target_include_directories(test_cli_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_cli_parser COMMAND test_cli_parser)
set_tests_properties(test_cli_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_input_parser
    test_input_parser.cpp
    ${PROJECT_ROOT}/src/parser/InputParser.cpp
    ${PROJECT_ROOT}/src/parser/CLIParser.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
)
target_include_directories(test_input_parser PRIVATE ${TEST_INCLUDE})
add_test(NAME test_input_parser COMMAND test_input_parser)
set_tests_properties(test_input_parser PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_output_formatter
    test_output_formatter.cpp
    ${PROJECT_ROOT}/src/parser/OutputFormatter.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/EnchSolution.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentRegistry.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
    ${PROJECT_ROOT}/src/algorithm/BaseAlgorithm.cpp
)
target_include_directories(test_output_formatter PRIVATE ${TEST_INCLUDE})
add_test(NAME test_output_formatter COMMAND test_output_formatter)
set_tests_properties(test_output_formatter PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

add_executable(test_integration
    test_integration.cpp
    ${PROJECT_ROOT}/src/parser/CLIParser.cpp
    ${PROJECT_ROOT}/src/parser/InputParser.cpp
    ${PROJECT_ROOT}/src/parser/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parser/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/parser/TagResolver.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/parser/OutputFormatter.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/EnchSolution.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentRegistry.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
    ${PROJECT_ROOT}/src/algorithm/BaseAlgorithm.cpp
)
target_include_directories(test_integration PRIVATE ${TEST_INCLUDE})
add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)

# ============================================================================
# New algorithm layer tests (to be enabled as implementation proceeds)
# ============================================================================

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_forge_engine.cpp)
add_executable(test_forge_engine
    test_forge_engine.cpp
    ${PROJECT_ROOT}/src/algorithm/DefaultForgeEngine.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
)
target_include_directories(test_forge_engine PRIVATE ${TEST_INCLUDE})
add_test(NAME test_forge_engine COMMAND test_forge_engine)
set_tests_properties(test_forge_engine PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_exp_calculator.cpp)
add_executable(test_exp_calculator
    test_exp_calculator.cpp
    ${PROJECT_ROOT}/src/utils/ExpCalculator.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
)
target_include_directories(test_exp_calculator PRIVATE ${TEST_INCLUDE})
add_test(NAME test_exp_calculator COMMAND test_exp_calculator)
set_tests_properties(test_exp_calculator PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_execution_context.cpp)
add_executable(test_execution_context
    test_execution_context.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
)
target_include_directories(test_execution_context PRIVATE ${TEST_INCLUDE})
add_test(NAME test_execution_context COMMAND test_execution_context)
set_tests_properties(test_execution_context PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_algorithm_registry.cpp)
add_executable(test_algorithm_registry
    test_algorithm_registry.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmRegistry.cpp
    ${PROJECT_ROOT}/src/algorithm/DefaultForgeEngine.cpp
)
target_include_directories(test_algorithm_registry PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_registry COMMAND test_algorithm_registry)
set_tests_properties(test_algorithm_registry PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_algorithm_executor.cpp)
add_executable(test_algorithm_executor
    test_algorithm_executor.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmExecutor.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmRegistry.cpp
    ${PROJECT_ROOT}/src/algorithm/DefaultForgeEngine.cpp
)
target_include_directories(test_algorithm_executor PRIVATE ${TEST_INCLUDE})
add_test(NAME test_algorithm_executor COMMAND test_algorithm_executor)
set_tests_properties(test_algorithm_executor PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_greedy_algorithm.cpp)
add_executable(test_greedy_algorithm
    test_greedy_algorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/GreedyAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmExecutor.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmRegistry.cpp
    ${PROJECT_ROOT}/src/algorithm/DefaultForgeEngine.cpp
    ${PROJECT_ROOT}/src/utils/ExpCalculator.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/EquipmentType.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
)
target_include_directories(test_greedy_algorithm PRIVATE ${TEST_INCLUDE})
add_test(NAME test_greedy_algorithm COMMAND test_greedy_algorithm)
set_tests_properties(test_greedy_algorithm PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
endif()
