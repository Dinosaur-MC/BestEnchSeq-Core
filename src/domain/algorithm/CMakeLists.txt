# ── Algorithm domain ──────────────────────────────────────────────────
# Compact types, forge engine, strategies, diagnostics, serialization,
# plugin loader.  Depends on besq-common-core + optional log.
#
# besq-algo-core is the ALGORITHM KERNEL — a SHARED library (算法内核).
# The parent (besq via besq-core), the sandbox worker (besq-worker), and
# plugins ALL link this same shared lib, so the kernel (DiagnosticsService,
# IAlgorithm vtables, singletons) resolves to ONE instance per process.
#
# A bare-plugin + -rdynamic model was tried (static kernel, plugins resolve
# against the host) but the large parent exe's -rdynamic export table caused
# a dynamic-linker relocation crash (SEGV) when dlopening plugins — the
# small worker was fine.  A shared kernel avoids -rdynamic entirely.

file(GLOB_RECURSE BESQ_ALGORITHM_SRCS CONFIGURE_DEPENDS "*.cpp" "*.hpp")
list(FILTER BESQ_ALGORITHM_SRCS EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/(plugin/|_strategies/)")

# ── Plugin loader (AlgorithmLoader.cpp) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt)

# ── Strategies (auto-discovered + registration) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/_strategies/CMakeLists.txt)

# ── besq-algo-core (SHARED) — the algorithm kernel ────────────────────
add_library(besq-algo-core SHARED
    ${BESQ_ALGORITHM_SRCS}
    ${BESQ_PLUGIN_SRCS}
    ${BESQ_BUILTIN_STRATEGY_SRCS}
)
target_link_libraries(besq-algo-core PUBLIC
    besq-common-core          # NSID types in public API
    besq-common-thread        # ThreadPool for parallel DPMerge
)
target_link_libraries(besq-algo-core PRIVATE
    besq-common-log           # internal logging (optional with BESQ_DISABLE_LOGGER)
    besq-common-io            # Json — via AppConfig.h in SandboxedExecutor/AlgorithmLoader
)
target_include_directories(besq-algo-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/algorithm/types/Enchantment.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/common>     # → #include "serialization/ISerializable.h"
)
target_compile_definitions(besq-algo-core PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
)
# dl for plugin loading — AlgorithmLoader (dlopen/dlsym) lives HERE, so the
# sandbox worker (links only algo-core) gets -ldl too.
if(UNIX AND NOT APPLE)
    target_link_libraries(besq-algo-core PUBLIC ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    # Export the kernel symbols so plugins can resolve IAlgorithm vtables.
    set_property(TARGET besq-algo-core PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
# Stable output paths so the plugin config can reference them on all platforms:
#   Windows  DLL → build/bin/besq-algo-core.dll, import lib → build/besq-algo-core.lib
#   Linux    .so → build/libbesq-algo-core.so
set_target_properties(besq-algo-core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
