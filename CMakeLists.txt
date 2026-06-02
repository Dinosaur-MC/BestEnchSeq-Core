cmake_minimum_required(VERSION 3.16)
project(BestEnchSeq-Core
    VERSION 0.0.1
    DESCRIPTION "BestEnchSeq Core - the console version of BestEnchSeq."
    HOMEPAGE_URL "https://github.com/Dinosaur-MC/BestEnchSeq-Core"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Metadata Options
option(ENABLE_NETWORK "Enable network" ON)

if(ENABLE_NETWORK)
    set(ENABLED_NETWORK true)
else()
    set(ENABLED_NETWORK false)
endif()

option(ENABLE_API_SERVICE "Enable API service" OFF)

if(ENABLE_NETWORK AND ENABLE_API_SERVICE)
    set(ENABLED_API true)
else()
    set(ENABLED_API false)
endif()

# Executable Output Path
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

file(GLOB_RECURSE MAIN_SRC_LIST CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
add_executable(${PROJECT_NAME} ${MAIN_SRC_LIST})

enable_testing()

add_executable(json_tests
    tests/json_tests.cpp
    src/io/json.cpp
)

target_include_directories(json_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME json_tests COMMAND json_tests)
set_tests_properties(json_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Parser test targets

add_executable(test_parser_utils
    tests/test_parser_utils.cpp
)
target_include_directories(test_parser_utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_parser_utils COMMAND test_parser_utils)
set_tests_properties(test_parser_utils PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_tag_resolver
    tests/test_tag_resolver.cpp
)
target_include_directories(test_tag_resolver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_tag_resolver COMMAND test_tag_resolver)
set_tests_properties(test_tag_resolver PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_ench_info_parser
    tests/test_ench_info_parser.cpp
)
target_include_directories(test_ench_info_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_ench_info_parser COMMAND test_ench_info_parser)
set_tests_properties(test_ench_info_parser PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_equipment_parser
    tests/test_equipment_parser.cpp
)
target_include_directories(test_equipment_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_equipment_parser COMMAND test_equipment_parser)
set_tests_properties(test_equipment_parser PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_cli_parser
    tests/test_cli_parser.cpp
)
target_include_directories(test_cli_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_cli_parser COMMAND test_cli_parser)
set_tests_properties(test_cli_parser PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_input_parser
    tests/test_input_parser.cpp
)
target_include_directories(test_input_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_input_parser COMMAND test_input_parser)
set_tests_properties(test_input_parser PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_output_formatter
    tests/test_output_formatter.cpp
)
target_include_directories(test_output_formatter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME test_output_formatter COMMAND test_output_formatter)
set_tests_properties(test_output_formatter PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
