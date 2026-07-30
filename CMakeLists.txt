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
# Compiler — project requires LLVM/Clang (or MSVC as fallback on Windows)
# ═══════════════════════════════════════════════════════════════════════════════
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    message(FATAL_ERROR
        "BestEnchSeq-Core requires LLVM/Clang (or MSVC on Windows).\n"
        "  Detected: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})\n"
        "  Use CMake presets to select the correct toolchain:\n"
        "    cmake --preset cli          # default (Release)\n"
        "    cmake --preset debug        # debug + tests\n"
        "  Or specify the compiler explicitly:\n"
        "    cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Compiler flags
# ═══════════════════════════════════════════════════════════════════════════════
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/FS)
    add_compile_options(/W4)
    add_compile_options("/utf-8")
elseif(UNIX OR MINGW)
    # -fPIC required for shared library (libbesq-core.so) on Linux
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-designated-field-initializers -fPIC)
else()
    # Windows + Clang-cl or other targets
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-designated-field-initializers)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

# ── Runtime linking mode for MinGW ──
option(BESQ_STATIC_RUNTIME "Statically link MinGW runtime DLLs (Windows)" OFF)

if(WIN32 AND NOT MSVC AND BESQ_STATIC_RUNTIME)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic")
endif()

if(WIN32 AND NOT MSVC AND NOT BESQ_STATIC_RUNTIME)
    get_filename_component(BESQ_COMPILER_REAL "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(BESQ_COMPILER_DIR "${BESQ_COMPILER_REAL}" DIRECTORY)
    set(BESQ_RUNTIME_DLL_DIR "${BESQ_COMPILER_DIR}" CACHE PATH "Directory with MinGW runtime DLLs")
    if(NOT EXISTS "${BESQ_RUNTIME_DLL_DIR}/libwinpthread-1.dll")
        get_filename_component(BESQ_PREFIX "${BESQ_COMPILER_DIR}" DIRECTORY)
    endif()
    message(STATUS "MinGW runtime DLL directory: ${BESQ_RUNTIME_DLL_DIR}")
endif()

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
option(BESQ_DISABLE_DIAGNOSTICS "Remove diagnostics code at compile time" OFF)
option(BESQ_BUILD_CLI "Build the besq CLI executable" ON)
option(BUILD_TESTING "Build test targets" ON)
option(BUILD_BENCHMARKS "Build benchmark targets" OFF)
set(BESQ_MAX_SOLUTIONS 32 CACHE STRING "Maximum solutions stored per algorithm execution (presets override this)")

set(CMAKE_DEFAULT_BUILD_TARGET besq)

# ═══════════════════════════════════════════════════════════════════════════════
# Generated config header (BuildConfig.h)
# ═══════════════════════════════════════════════════════════════════════════════
set(BESQ_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BuildConfig.h.in"
    "${BESQ_GENERATED_DIR}/BuildConfig.h"
)

# ═══════════════════════════════════════════════════════════════════════════════
# Embedded data (OBJECT library — reusable across targets)
# ═══════════════════════════════════════════════════════════════════════════════
include(${CMAKE_SOURCE_DIR}/cmake/EmbedResource.cmake)

embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/vanilla.json"
    SYMBOL vanilla_json
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_vanilla.cpp"
)
embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/item_properties.json"
    SYMBOL item_properties
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_item_props.cpp"
)
embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/zh_CN.json"
    SYMBOL i18n_zh_CN
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_i18n_zh_CN.cpp"
)
embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/en_US.json"
    SYMBOL i18n_en_US
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_i18n_en_US.cpp"
)
embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/minecraft/zh_CN.json"
    SYMBOL mc_i18n_zh_CN
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_mc_i18n_zh_CN.cpp"
)
embed_resource(
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/minecraft/en_US.json"
    SYMBOL mc_i18n_en_US
    OUTPUT "${BESQ_GENERATED_DIR}/EmbeddedData_mc_i18n_en_US.cpp"
)

set(BESQ_EMBEDDED_SRCS
    "${BESQ_GENERATED_DIR}/EmbeddedData_vanilla.cpp"
    "${BESQ_GENERATED_DIR}/EmbeddedData_item_props.cpp"
    "${BESQ_GENERATED_DIR}/EmbeddedData_i18n_zh_CN.cpp"
    "${BESQ_GENERATED_DIR}/EmbeddedData_i18n_en_US.cpp"
    "${BESQ_GENERATED_DIR}/EmbeddedData_mc_i18n_zh_CN.cpp"
    "${BESQ_GENERATED_DIR}/EmbeddedData_mc_i18n_en_US.cpp"
)

# ═══════════════════════════════════════════════════════════════════════════════
# Sub-projects (static libraries per domain)
# ═══════════════════════════════════════════════════════════════════════════════
add_subdirectory(src/common)
add_subdirectory(src/domain)

# ── besq-core (shared library) ──────────────────────────────────────────────
# Links all domain libraries together.  On Windows, auto-export all symbols
# so loaded plugins can resolve IAlgorithm / ForgeEngine vtables.
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(besq-core SHARED
    ${BESQ_EMBEDDED_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/DataLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/ItemProperties.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/I18nLoader.cpp
)

target_link_libraries(besq-core PUBLIC
    besq-domain-interface   # PUBLIC so transitive include paths (via orchestration → algorithm → common-core) propagate to consumers
)

target_include_directories(besq-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>   # → #include "besq/besq.h"
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>       # → #include "domain/algorithm/..."
    $<BUILD_INTERFACE:${BESQ_GENERATED_DIR}>                 # → #include "BuildConfig.h"
)

target_compile_definitions(besq-core PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
)

# Link dl library for plugin loading
if(UNIX AND NOT APPLE)
    target_link_libraries(besq-core PUBLIC ${CMAKE_DL_LIBS})
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Export for plugin build system — auto-generated config for plugins/
# ═══════════════════════════════════════════════════════════════════════════════

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/besq-coreConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/besq-coreConfig.cmake"
    @ONLY
)

# ═══════════════════════════════════════════════════════════════════════════════
# besq-minimal (static library for embedders)
# ═══════════════════════════════════════════════════════════════════════════════
# Compact algorithm core with Hamming strategy only.  Links only
# besq-common-core (NSID, LogLevel, memory pools).  No plugin loader,
# serialization, Logger, i18n, CLI parser, or embedded data.
#
# Build with: cmake --preset minimal && cmake --build build --preset minimal

file(GLOB_RECURSE BESQ_MINIMAL_ALGO_SRCS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/domain/algorithm/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/domain/algorithm/*.hpp"
)
# Exclude plugin loader and serialization
list(FILTER BESQ_MINIMAL_ALGO_SRCS EXCLUDE REGEX
    "${CMAKE_SOURCE_DIR}/src/domain/algorithm/(plugin/|serialization/)")
# Exclude all built-in strategies — add back only hamming below
list(FILTER BESQ_MINIMAL_ALGO_SRCS EXCLUDE REGEX
    "${CMAKE_SOURCE_DIR}/src/domain/algorithm/_strategies/")

# Add Hamming strategy
file(GLOB BESQ_MINIMAL_HAMMING_SRCS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/domain/algorithm/_strategies/hamming/*.cpp")

# Minimal strategy registration (hamming only)
set(BESQ_MINIMAL_REG "${BESQ_GENERATED_DIR}/_minimal_registration.cpp")
file(WRITE "${BESQ_MINIMAL_REG}"
    "// GENERATED -- do not edit\n"
    "#include \"domain/algorithm/_strategies/Registration.h\"\n"
    "#include \"domain/algorithm/_strategies/hamming/HammingAlgorithm.h\"\n"
    "\n"
    "void besq_register_builtin_strategies(algorithm::AlgorithmRegistry &reg) {\n"
    "    reg.register_algorithm(\"hamming\", []{ return std::make_unique<algorithm::HammingAlgorithm>(); });\n"
    "}\n"
)

add_library(besq-minimal STATIC
    ${BESQ_MINIMAL_ALGO_SRCS}
    ${BESQ_MINIMAL_HAMMING_SRCS}
    "${BESQ_MINIMAL_REG}"
)

target_link_libraries(besq-minimal PUBLIC besq-common-core)

target_include_directories(besq-minimal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${BESQ_GENERATED_DIR}>
)

target_compile_definitions(besq-minimal PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
)

# ═══════════════════════════════════════════════════════════════════════════════
# CLI executable (guarded by BESQ_BUILD_CLI)
# ═══════════════════════════════════════════════════════════════════════════════
if(BESQ_BUILD_CLI)
    add_executable(besq
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    )
    target_link_libraries(besq PRIVATE besq-core)
    target_include_directories(besq PRIVATE ${BESQ_GENERATED_DIR})

    if(UNIX)
        target_link_options(besq PRIVATE -rdynamic)
    endif()

    target_compile_definitions(besq PRIVATE
        $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install rules
# ═══════════════════════════════════════════════════════════════════════════════
include(GNUInstallDirs)
install(TARGETS besq-core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(BESQ_BUILD_CLI)
    install(TARGETS besq
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(FILES ${CMAKE_SOURCE_DIR}/include/besq/besq.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/besq
)

install(DIRECTORY data/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/BestEnchSeq-Core/data
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests & Benchmarks (optional)
# ═══════════════════════════════════════════════════════════════════════════════
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
