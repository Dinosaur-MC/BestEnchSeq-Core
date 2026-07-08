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

if(MSVC)
    add_compile_options(/FS)
endif()

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

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Tests
enable_testing()
add_subdirectory(tests)

# Benchmarks (optional, not part of test suite)
add_subdirectory(benchmarks)
