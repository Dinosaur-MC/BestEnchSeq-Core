cmake_minimum_required(VERSION 3.16)
project(BestEnchSeq-Core
    VERSION 0.0.1
    DESCRIPTION "BestEnchSeq Core - the console version of BestEnchSeq."
    HOMEPAGE_URL "https://github.com/Dinosaur-MC/BestEnchSeq-Core"
    LANGUAGES CXX
)

# ═══════════════════════════════════════════════════════════════════════════════
# C++ Standard
# ═══════════════════════════════════════════════════════════════════════════════
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ═══════════════════════════════════════════════════════════════════════════════
# Compiler flags
# ═══════════════════════════════════════════════════════════════════════════════
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/FS)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Shorten __FILE__ to project-relative paths
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

# ── Runtime linking mode for MinGW / Clang-GNU on Windows ──
# Clang with GNU command-line on Windows uses MinGW runtime DLLs
# (libwinpthread-1.dll, libstdc++-6.dll).  These may not be on the
# system PATH.  BESQ_STATIC_RUNTIME=ON statically links them into
# the executable; OFF (default) relies on PATH at runtime.
option(BESQ_STATIC_RUNTIME "Statically link MinGW runtime DLLs (Windows)" OFF)

if(WIN32 AND NOT MSVC AND BESQ_STATIC_RUNTIME)
    # Clang in GNU mode needs -fuse-ld=ld to use the GNU linker for -static flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic")
endif()

# When using dynamic runtime DLLs, detect the compiler's runtime directory
# so ctest can find the DLLs via the ENVIRONMENT test property.
if(WIN32 AND NOT MSVC AND NOT BESQ_STATIC_RUNTIME)
    get_filename_component(BESQ_COMPILER_REAL "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(BESQ_COMPILER_DIR "${BESQ_COMPILER_REAL}" DIRECTORY)
    set(BESQ_RUNTIME_DLL_DIR "${BESQ_COMPILER_DIR}" CACHE PATH "Directory with MinGW runtime DLLs")
    if(NOT EXISTS "${BESQ_RUNTIME_DLL_DIR}/libwinpthread-1.dll")
        get_filename_component(BESQ_PREFIX "${BESQ_COMPILER_DIR}" DIRECTORY)
        foreach(_try "${BESQ_PREFIX}/mingw64/bin" "D:/Local/Strawberry/c/bin")
            if(EXISTS "${_try}/libwinpthread-1.dll")
                set(BESQ_RUNTIME_DLL_DIR "${_try}" CACHE PATH "MinGW runtime DLL directory" FORCE)
                break()
            endif()
        endforeach()
    endif()
    message(STATUS "MinGW runtime DLL directory: ${BESQ_RUNTIME_DLL_DIR}")
endif()

# Executable output path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# ═══════════════════════════════════════════════════════════════════════════════
# Build type default
# ═══════════════════════════════════════════════════════════════════════════════
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Options
# ═══════════════════════════════════════════════════════════════════════════════

option(BESQ_DISABLE_DIAGNOSTICS "Remove diagnostics code at compile time"               OFF)
option(BESQ_BUILD_CLI           "Build the besq CLI executable"                           ON)
option(BUILD_TESTING            "Build test targets"                                      ON)
option(BUILD_BENCHMARKS         "Build benchmark targets"                                 OFF)
set  (BESQ_MAX_SOLUTIONS 32     CACHE STRING "Maximum solutions stored per algorithm execution")

# Default target for bare `cmake --build build` — builds only the CLI (fast).
set(CMAKE_DEFAULT_BUILD_TARGET besq)

# ═══════════════════════════════════════════════════════════════════════════════
# Shared include directories
# ═══════════════════════════════════════════════════════════════════════════════
set(BESQ_PUBLIC_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ═══════════════════════════════════════════════════════════════════════════════
# Generated config header (BuildConfig.h)
# ═══════════════════════════════════════════════════════════════════════════════
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config/BuildConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.h"
)
set(BESQ_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ═══════════════════════════════════════════════════════════════════════════════
# Source file groups — each functional area is a CMake variable so library
# presets can pick exactly what they need.
#
# Types and algorithm-layer source lists live in their own sub-CMakeLists:
#   src/types/CMakeLists.txt, src/algorithm/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════

include("${CMAKE_SOURCE_DIR}/src/types/CMakeLists.txt")
include("${CMAKE_SOURCE_DIR}/src/algorithm/CMakeLists.txt")

# ── I/O ──
set(BESQ_IO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/CsvIO.cpp
)

# ── Logger ──
set(BESQ_LOG_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log/Logger.cpp
)

# ── Domain registries ──
set(BESQ_REG_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/EnchantmentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/EquipmentCategoryRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/EquipmentRegistry.cpp
)
set(BESQ_COMPACT_REG_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/CompactedRegistries.cpp
)
set(BESQ_ALGO_REG_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/AlgorithmRegistry.cpp
)
set(BESQ_REG_MGR_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/RegistryManager.cpp
)

# ── Parsers ──
set(BESQ_PARSER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/EnchParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/ItemParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/EnchInfoParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/CLIParser.cpp
)

# ── Resolvers ──
set(BESQ_RESOLVER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resolvers/ItemResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resolvers/InventoryResolver.cpp
)

# ── Adapters ──
set(BESQ_ADAPTER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/CompactAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/RawTypeAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/OutputFormatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/EnchSerializer.cpp
)

# ── Data ──
set(BESQ_DATA_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data/DataLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data/ItemProperties.cpp
)

# ── CLI (executable only) ──
set(BESQ_CLI_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli/cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli/RegistryEditor.cpp
)

# ── API (library public face) ──
set(BESQ_API_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/BesqContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/SolvePipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/ProfileSet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/CAbiBindings.cpp
)
set(BESQ_ALGO_REG_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registries/AlgorithmRegistration.cpp
)

# ── All strategies combined (convenience for core targets) ──
set(BESQ_ALL_STRATEGY_SRCS
    ${BESQ_STRATEGY_GREEDY_SRCS}
    ${BESQ_STRATEGY_DFS_SRCS}
    ${BESQ_STRATEGY_ASTAR_SRCS}
    ${BESQ_STRATEGY_IDASTAR_SRCS}
    ${BESQ_STRATEGY_HAMMING_SRCS}
    ${BESQ_STRATEGY_HIERARCHICAL_SRCS}
    ${BESQ_STRATEGY_PENALTY_BALANCE_SRCS}
    ${BESQ_STRATEGY_DIFF_FIRST_SRCS}
)

# ═══════════════════════════════════════════════════════════════════════════════
# Embedded data (OBJECT library — reusable across targets)
# ═══════════════════════════════════════════════════════════════════════════════

include(${CMAKE_SOURCE_DIR}/cmake/EmbedResource.cmake)

embed_resource(
    INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/vanilla.json"
    SYMBOL vanilla_json
    OUTPUT "${PROJECT_BINARY_DIR}/generated/EmbeddedData_vanilla.cpp"
)
embed_resource(
    INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/item_properties.json"
    SYMBOL item_properties
    OUTPUT "${PROJECT_BINARY_DIR}/generated/EmbeddedData_item_props.cpp"
)

set(BESQ_EMBEDDED_SRCS
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData_vanilla.cpp"
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData_item_props.cpp"
)

# ═══════════════════════════════════════════════════════════════════════════════
# Library targets
# ═══════════════════════════════════════════════════════════════════════════════

# ── besq-minimal ──────────────────────────────────────────────────────────────
#  Algorithm-only static library.  Compact types, registries, algorithm core
#  (forge + components), and the Hamming strategy.  No logger, no diagnostics,
#  no parsers, no I/O.  Consumer provides AlgorithmInput programmatically.
add_library(besq-minimal STATIC
    ${BESQ_TYPE_SRCS}
    ${BESQ_COMPACT_TYPE_SRCS}
    ${BESQ_REG_SRCS}
    ${BESQ_COMPACT_REG_SRCS}
    ${BESQ_ALGO_REG_SRCS}
    ${BESQ_ALGO_CORE_SRCS}
    ${BESQ_ALGO_FORGE_SRCS}
    ${BESQ_STRATEGY_HAMMING_SRCS}
    ${BESQ_ALGO_REG_SRC}
)
target_include_directories(besq-minimal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BESQ_GENERATED_DIR}
)
target_compile_definitions(besq-minimal PUBLIC
    BESQ_DISABLE_LOGGER
    BESQ_DISABLE_DIAGNOSTICS
    BESQ_MAX_SOLUTIONS=32
    BESQ_HAVE_HAMMING
)

# ── besq-core ─────────────────────────────────────────────────────────────────
#  Full library suitable for embedding.  All strategies, API layer, I/O,
#  resolvers, adapters, logging, diagnostics.  No CLI, no serialization.
add_library(besq-core STATIC
    ${BESQ_EMBEDDED_SRCS}
    ${BESQ_TYPE_SRCS}
    ${BESQ_COMPACT_TYPE_SRCS}
    ${BESQ_IO_SRCS}
    ${BESQ_LOG_SRCS}
    ${BESQ_REG_SRCS}
    ${BESQ_COMPACT_REG_SRCS}
    ${BESQ_ALGO_REG_SRCS}
    ${BESQ_ALGO_CORE_SRCS}
    ${BESQ_ALGO_FORGE_SRCS}
    ${BESQ_ALGO_DIAG_SRCS}
    ${BESQ_ALGO_SERIAL_SRCS}
    ${BESQ_ALL_STRATEGY_SRCS}
    ${BESQ_PARSER_SRCS}
    ${BESQ_RESOLVER_SRCS}
    ${BESQ_ADAPTER_SRCS}
    ${BESQ_DATA_SRCS}
    ${BESQ_API_SRCS}
    ${BESQ_ALGO_REG_SRC}
)
target_include_directories(besq-core PUBLIC
    ${BESQ_PUBLIC_INCLUDES}
    ${BESQ_GENERATED_DIR}
)
target_compile_definitions(besq-core PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    BESQ_HAVE_GREEDY
    BESQ_HAVE_DFS
    BESQ_HAVE_ASTAR
    BESQ_HAVE_IDASTAR
    BESQ_HAVE_HAMMING
    BESQ_HAVE_HIERARCHICAL
    BESQ_HAVE_PENALTY_BALANCE
    BESQ_HAVE_DIFF_FIRST
)
# ═══════════════════════════════════════════════════════════════════════════════
# CLI executable  (guarded by BESQ_BUILD_CLI)
# ═══════════════════════════════════════════════════════════════════════════════

if(BESQ_BUILD_CLI)
    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${BESQ_CLI_SRCS}
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "besq")
    target_link_libraries(${PROJECT_NAME} PRIVATE besq-core)
    target_include_directories(${PROJECT_NAME} PRIVATE ${BESQ_GENERATED_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    )
endif()

# Install rules
include(GNUInstallDirs)
install(TARGETS besq-core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(BESQ_BUILD_CLI)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(DIRECTORY data/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/BestEnchSeq-Core/data
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests (optional, enabled by default)
# ═══════════════════════════════════════════════════════════════════════════════

option(BUILD_TESTING "Build test targets" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Benchmarks (optional, not part of test suite)
# ═══════════════════════════════════════════════════════════════════════════════

option(BUILD_BENCHMARKS "Build benchmark targets" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
