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

# Export Compile Commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/FS)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Shorten __FILE__ to project-relative paths (e.g. "src/parsers/InputParser.cpp")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

# Executable Output Path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# ── Collect library sources (all src/*.cpp except main.cpp) ──
file(GLOB_RECURSE ALL_SRC_LIST CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER ALL_SRC_LIST EXCLUDE REGEX ".*/main\\.cpp$")

# ── Shared include directories ──
set(BESQ_PUBLIC_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Static library (linkable by test targets and external consumers) ──
add_library(besq STATIC ${ALL_SRC_LIST})
target_include_directories(besq PUBLIC ${BESQ_PUBLIC_INCLUDES})
target_include_directories(besq PRIVATE ${BESQ_GENERATED_DIR})
target_compile_definitions(besq PUBLIC BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS})

# ── CLI executable (thin wrapper linking the library) ──
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE besq)
# CLI output name: "besq" matches documentation; PROJECT_NAME for CMake-internal
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "besq")
target_include_directories(${PROJECT_NAME} PRIVATE ${BESQ_GENERATED_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
)

# Data embedding (vanilla.json compiled into the binary)
add_subdirectory(data)

# Install rules
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY data/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/BestEnchSeq-Core/data
)

# ============================================================================
# Compile-time configuration
# ============================================================================

# ── Options ──
option(BESQ_DISABLE_DIAGNOSTICS "Remove diagnostics code at compile time" OFF)
set(BESQ_MAX_SOLUTIONS 128 CACHE STRING "Maximum solutions stored per algorithm execution")

# ── Generated config header (version info) ──
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config/BuildConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.h"
)

set(BESQ_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(besq PRIVATE ${BESQ_GENERATED_DIR})

# ── Compile definitions (propagate to library + executable + tests) ──
target_compile_definitions(besq PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
)

# ── Build type default ──
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Tests
# ============================================================================
enable_testing()
add_subdirectory(tests)

# ============================================================================
# Benchmarks (optional, not part of test suite)
# ============================================================================
add_subdirectory(benchmarks)
