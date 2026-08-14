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

# Clang 下限 18：编译期路由校验（HttpController.h 的 consteval validate_routes）
# 依赖完整 consteval 实现，Clang < 18 支持不完整（官方不定义 __cpp_consteval，
# 已知 "not a constant expression" 误报与运行时推迟类 CodeGen 崩溃）。
# 仅约束纯 LLVM Clang（AppleClang 版本号体系不同，不在此限）。
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR
        "BestEnchSeq-Core requires Clang 18+ (consteval route validation\n"
        "  is not reliable before Clang 18).\n"
        "  Detected: Clang ${CMAKE_CXX_COMPILER_VERSION}\n"
        "  Install Clang 18 or newer (e.g. apt install clang-18 or the\n"
        "  official LLVM installer) and re-run:\n"
        "    cmake --preset full          # or cli/debug\n"
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
    # 不列 -Wno-missing-designated-field-initializers：它是 -Wmissing-field-initializers
    # 的子警告（clang 19+），父组 -Wno- 已覆盖；clang 18 无此独立开关，列了反而告警
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers -fPIC)
else()
    # Windows + Clang-cl or other targets
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
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
option(BESQ_BUILD_GUI "Build the besq-gui Web GUI executable" OFF)
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
# Embedded resources (generated access layer)
# ═══════════════════════════════════════════════════════════════════════════════
# The embedded-resource access layer is fully generated from the
# besq_embed_resources() declarations below (single source of truth):
#   - shared header  ${BESQ_GENERATED_DIR}/builtin/EmbeddedResources_generated.h
#     (enum class ResourceId + constexpr raw()/resource_name()/group_of())
#   - one implementation per group, compiled into the group's target
#     (data → besq-core, frontend → besq-gui; other groups return empty views)
# See cmake/EmbedResource.cmake for the API and validation rules.
include(${CMAKE_SOURCE_DIR}/cmake/EmbedResource.cmake)

# ═══════════════════════════════════════════════════════════════════════════════
# Sub-projects (static libraries per domain)
# ═══════════════════════════════════════════════════════════════════════════════
add_subdirectory(src/common)
add_subdirectory(src/domain)

# ═══════════════════════════════════════════════════════════════════════════════
# besq-core (STATIC) — BESQ 核心: 算法域 + 业务域 + 编排域 + 内嵌数据 (排除接口域)
# ═══════════════════════════════════════════════════════════════════════════════
# Compiles the embedded data resources (generated data_assets.cpp) directly into
# the core, and links the three non-interface domains.  Statically linked into
# hosts (the CLI), never a shared DLL.  The interface domain is added by the
# CLI executable.
add_library(besq-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtin/BuiltinCore.cpp
)
target_link_libraries(besq-core PUBLIC
    besq-domain-orchestration   # links besq-algo-core + besq-domain-business
    besq-domain-business
    besq-algo-core
)

# Data resource group (compiled into besq-domain-business, next to the
# BuiltinData loader that consumes it — keeps the archive self-contained so
# UNIX links need no --start-group; besq-core stays a pure aggregate).
besq_embed_resources(
    GROUPS
        data besq-domain-business
    RESOURCES
        data_vanilla_json=${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/vanilla.json
        data_item_properties=${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/item_properties.json
        data_i18n_zh_CN=${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/zh_CN.json
        data_i18n_en_US=${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/en_US.json
        data_mc_i18n_zh_CN=${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/minecraft/zh_CN.json
        data_mc_i18n_en_US=${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/minecraft/en_US.json
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
    # Dependency order: interface first, then the core aggregate — the
    # business archive is self-contained (BuiltinData + embedded data live in
    # besq-domain-business), so plain left-to-right static resolution works on
    # both GNU ld and lld with no --start-group.
    target_link_libraries(besq PRIVATE
        besq-domain-interface
        besq-core
    )
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
# besq-gui (guarded by BESQ_BUILD_GUI) — Web GUI host (browser host, v1; WebView2 future work)
# ═══════════════════════════════════════════════════════════════════════════════
if(BESQ_BUILD_GUI)
    # Embedded frontend assets (single-exe distribution): declared once in the
    # frontend resource group below — the generator emits the shared enum +
    # frontend_assets.cpp (compiled into besq-gui).  The kAssets URL table in
    # src/gui/main.cpp maps URLs to ResourceId members and MUST stay in sync
    # with the member names declared here.
    set(GUI_FRONTEND ${CMAKE_CURRENT_SOURCE_DIR}/gui/frontend)
    # 图标只嵌 sprite sheet 单图（前端按 sprite.js 定位裁剪）；
    # 源图 assets/item_icons/，重生成: python3 scripts/gen_sprite.py

    add_executable(besq-gui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main.cpp
    )
    # Frontend resource group.  The implementation is compiled into
    # besq-domain-business (same archive as the data group): the inline
    # raw() dispatcher references EVERY group's accessor at every call site
    # (no dead-branch elimination in Debug), so all definitions must live in
    # a library every consumer links — otherwise CLI/GUI would need separate
    # frontend_raw/data_raw definitions and statically collide.
    besq_embed_resources(
        GROUPS
            frontend besq-domain-business
        RESOURCES
            frontend_index_html=${GUI_FRONTEND}/index.html
            frontend_styles_css=${GUI_FRONTEND}/styles.css
            frontend_app_js=${GUI_FRONTEND}/app.js
            frontend_api_js=${GUI_FRONTEND}/api.js
            frontend_i18n_js=${GUI_FRONTEND}/i18n.js
            frontend_names_zh_js=${GUI_FRONTEND}/names_zh.js
            frontend_sprite_js=${GUI_FRONTEND}/sprite.js
            frontend_vendor_icons_sprite_png=${GUI_FRONTEND}/vendor/icons/sprite.png
            frontend_vendor_mdui_mdui_css=${GUI_FRONTEND}/vendor/mdui/mdui.css
            frontend_vendor_mdui_mdui_global_js=${GUI_FRONTEND}/vendor/mdui/mdui.global.js
            frontend_view_calculator=${GUI_FRONTEND}/views/calculator.js
            frontend_view_profiles=${GUI_FRONTEND}/views/profiles.js
            frontend_view_algorithms=${GUI_FRONTEND}/views/algorithms.js
            frontend_view_history=${GUI_FRONTEND}/views/history.js
            frontend_view_settings=${GUI_FRONTEND}/views/settings.js
            frontend_view_status=${GUI_FRONTEND}/views/status.js
            frontend_view_pager=${GUI_FRONTEND}/views/pager.js
    )
    # Left-to-right static resolution (no --start-group): interface first,
    # then the core aggregate — see the besq target above.
    target_link_libraries(besq-gui PRIVATE
        besq-domain-interface
        besq-core
    )
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
