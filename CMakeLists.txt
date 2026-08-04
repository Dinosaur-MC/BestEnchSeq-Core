cmake_minimum_required(VERSION 3.25)
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
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-designated-field-initializers -Wno-missing-field-initializers -fPIC)
else()
    # Windows + Clang-cl or other targets
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-designated-field-initializers -Wno-missing-field-initializers)
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

if(UNIX)
    # Shared libs (libbesq-algo-core.so) live in the build dir; give every
    # executable a build-tree RPATH so it finds them without LD_LIBRARY_PATH.
    set(CMAKE_BUILD_RPATH "${PROJECT_BINARY_DIR}")
endif()

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
option(BESQ_DEEP_DIAGNOSTICS "Enable per-operation diagnostic counters (profiling builds; default off = zero hot-path cost)" OFF)
option(BESQ_BUILD_CLI "Build the besq CLI executable" ON)
option(BESQ_BUILD_SANDBOX "Build the besq-worker sandbox executable (algorithm sandboxing for third-party plugins)" OFF)
option(BESQ_BUILD_GUI "Build the besq-gui WebView2 GUI executable" OFF)
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

# ═══════════════════════════════════════════════════════════════════════════════
# besq-core (STATIC) — BESQ 核心: 算法域 + 业务域 + 编排域 + 内嵌数据 (排除接口域)
# ═══════════════════════════════════════════════════════════════════════════════
# Compiles the built-in data bootstrap (DataLoader / ItemProperties / I18nLoader
# + embedded resources) directly into the core, and links the three non-interface
# domains.  Statically linked into hosts (the CLI), never a shared DLL.
# The interface domain is added by the CLI executable.
add_library(besq-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/DataLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/ItemProperties.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/I18nLoader.cpp
    ${BESQ_EMBEDDED_SRCS}
)
target_link_libraries(besq-core PUBLIC
    besq-domain-orchestration   # links besq-algo-core + besq-domain-business
    besq-domain-business
    besq-algo-core
)
target_link_libraries(besq-core PRIVATE
    besq-common-i18n            # LanguageManager for I18nLoader
)
target_include_directories(besq-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${BESQ_GENERATED_DIR}>
)
target_compile_definitions(besq-core PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
)
# dl for plugin loading comes from besq-algo-core (PUBLIC), which hosts the
# AlgorithmLoader.  Not linked here.

# ═══════════════════════════════════════════════════════════════════════════════
# Export for plugin build system — auto-generated config for plugins/
# ═══════════════════════════════════════════════════════════════════════════════

# Diagnostics macros propagated to plugin builds, so plugins compile
# ExecutionContext::incr_* consistently with the host (see spec §5 — an ODR
# divergence on inline functions would otherwise make plugin per-op counters
# silently no-op).  Includes BESQ_MAX_SOLUTIONS + the two diagnostics macros.
set(BESQ_PLUGIN_DIAG_DEFS "BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}")
if(BESQ_DEEP_DIAGNOSTICS)
    string(APPEND BESQ_PLUGIN_DIAG_DEFS ";BESQ_DEEP_DIAGNOSTICS")
endif()
if(BESQ_DISABLE_DIAGNOSTICS)
    string(APPEND BESQ_PLUGIN_DIAG_DEFS ";BESQ_DISABLE_DIAGNOSTICS")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/besq-coreConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/besq-coreConfig.cmake"
    @ONLY
)

# ═══════════════════════════════════════════════════════════════════════════════
# CLI executable (guarded by BESQ_BUILD_CLI)
# ═══════════════════════════════════════════════════════════════════════════════
# Links the static besq-core (three domains) + the interface domain + builtin
# data + embedded resources.  Exports its symbols (-rdynamic / ENABLE_EXPORTS)
# so in-process plugins resolve the algorithm kernel from this single copy.
if(BESQ_BUILD_CLI)
    add_executable(besq
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    )
    if(UNIX)
        # business ↔ builtin is a real circular static-lib dependency, and the
        # entry points (interface/orchestration → business → builtin) sit
        # across archives.  The group MUST bracket the WHOLE chain — core,
        # business, orchestration, AND interface — not just core+business
        # (that leaves interface outside, whose late business pulls then
        # reference builtin after core was already scanned).
        target_link_libraries(besq PRIVATE
            -Wl,--start-group besq-core besq-domain-business besq-domain-orchestration besq-domain-interface besq-algo-core -Wl,--end-group
        )
    else()
        target_link_libraries(besq PRIVATE besq-core besq-domain-interface)
    endif()
    target_include_directories(besq PRIVATE
        ${BESQ_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # → #include "besq/besq.h"
    )

    target_compile_definitions(besq PRIVATE
        $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
        $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# besq-worker (guarded by BESQ_BUILD_SANDBOX) — sandbox algorithm execution worker
# ═══════════════════════════════════════════════════════════════════════════════
# Links ONLY the algorithm kernel (besq-algo-core) + worker sources.  The
# kernel is SHARED — both the worker and plugins resolve it from the same
# libbesq-algo-core.so, so no -rdynamic export is needed.
if(BESQ_BUILD_SANDBOX)
    add_executable(besq-worker
        ${CMAKE_CURRENT_SOURCE_DIR}/src/worker/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/worker/sandbox_seccomp.cpp
    )
    target_link_libraries(besq-worker PRIVATE
        besq-algo-core
        besq-common-io    # ByteStream for the IPC wire format
    )
    target_include_directories(besq-worker PRIVATE ${BESQ_GENERATED_DIR})

    target_compile_definitions(besq-worker PRIVATE
        $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
        $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# besq-gui (guarded by BESQ_BUILD_GUI) — WebView2 GUI host (Windows-first)
# ═══════════════════════════════════════════════════════════════════════════════
if(BESQ_BUILD_GUI)
    # Embedded frontend assets (single-exe distribution)
    set(GUI_FRONTEND ${CMAKE_CURRENT_SOURCE_DIR}/gui/frontend)
    set(GUI_ASSET_NAMES index.html styles.css app.js api.js i18n.js)
    foreach(name IN LISTS GUI_ASSET_NAMES)
        string(REPLACE "." "_" sym "frontend_${name}")
        embed_resource(INPUT "${GUI_FRONTEND}/${name}" SYMBOL ${sym}
                       OUTPUT "${BESQ_GENERATED_DIR}/Frontend_${sym}.cpp")
        list(APPEND GUI_FRONTEND_SRCS "${BESQ_GENERATED_DIR}/Frontend_${sym}.cpp")
    endforeach()
    set(GUI_VIEW_NAMES calculator profiles algorithms logs settings status)
    foreach(name IN LISTS GUI_VIEW_NAMES)
        embed_resource(INPUT "${GUI_FRONTEND}/views/${name}.js" SYMBOL frontend_view_${name}
                       OUTPUT "${BESQ_GENERATED_DIR}/Frontend_view_${name}.cpp")
        list(APPEND GUI_FRONTEND_SRCS "${BESQ_GENERATED_DIR}/Frontend_view_${name}.cpp")
    endforeach()

    add_executable(besq-gui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main.cpp
        ${GUI_FRONTEND_SRCS}
    )
    # UNIX: the whole chain must be bracketed in one --start-group (same
    # circular static-lib rationale as the besq target above).
    if(UNIX)
        target_link_libraries(besq-gui PRIVATE
            -Wl,--start-group besq-core besq-domain-business besq-domain-orchestration besq-domain-interface besq-algo-core -Wl,--end-group
        )
    else()
        target_link_libraries(besq-gui PRIVATE besq-core besq-domain-interface)
    endif()
    target_include_directories(besq-gui PRIVATE
        ${BESQ_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(besq-gui PRIVATE
        $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
        $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install rules
# ═══════════════════════════════════════════════════════════════════════════════
include(GNUInstallDirs)
# besq-core is a STATIC aggregate of the three non-interface domains + builtin
# data — installed via its consumers (the CLI).  Nothing to install directly.

if(BESQ_BUILD_CLI)
    install(TARGETS besq
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(BESQ_BUILD_SANDBOX)
    install(TARGETS besq-worker
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(BESQ_BUILD_GUI)
    install(TARGETS besq-gui
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
