# ── Algorithm domain ──────────────────────────────────────────────────
# Compact types, forge engine, strategies, diagnostics, serialization,
# plugin loader.  Depends on besq-common-core + optional log.
#
# besq-algo-core is the ALGORITHM KERNEL — a STATIC library (算法内核).
# It is linked into hosts (besq-core → besq CLI, and besq-worker), which
# export its symbols (-rdynamic on Linux / ENABLE_EXPORTS on Windows).
# Plugins are built bare (compile against these headers, symbols left
# undefined) and resolve the kernel at dlopen against the host's single
# copy — so DiagnosticsService / IAlgorithm vtables unify per process.

file(GLOB_RECURSE BESQ_ALGORITHM_SRCS CONFIGURE_DEPENDS "*.cpp" "*.hpp")
list(FILTER BESQ_ALGORITHM_SRCS EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/(plugin/|_strategies/)")

# ── Plugin loader (AlgorithmLoader.cpp) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt)

# ── Strategies (auto-discovered + registration) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/_strategies/CMakeLists.txt)

# ── besq-algo-core (STATIC) — the algorithm kernel ────────────────────
add_library(besq-algo-core STATIC
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
# Stable archive path (root build dir) so the plugin config can reference it.
set_target_properties(besq-algo-core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
