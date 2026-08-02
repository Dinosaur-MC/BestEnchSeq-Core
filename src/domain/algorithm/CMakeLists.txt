# ── besq-domain-algorithm ──────────────────────────────────────────────────
# Algorithm domain: compact types, forge engine, strategies, diagnostics,
# serialization, plugin loader.  Depends on besq-common-core + optional log.

file(GLOB_RECURSE BESQ_ALGORITHM_SRCS CONFIGURE_DEPENDS "*.cpp" "*.hpp")
list(FILTER BESQ_ALGORITHM_SRCS EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/(plugin/|_strategies/)")

# ── Plugin loader (AlgorithmLoader.cpp) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt)

# ── Strategies (auto-discovered + registration) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/_strategies/CMakeLists.txt)

# ── Library target ─────────────────────────────────────────────────────────
add_library(besq-domain-algorithm STATIC
    ${BESQ_ALGORITHM_SRCS}
    ${BESQ_PLUGIN_SRCS}
    ${BESQ_BUILTIN_STRATEGY_SRCS}
)
target_link_libraries(besq-domain-algorithm PUBLIC
    besq-common-core          # NSID types in public API
    besq-common-thread        # ThreadPool for parallel DPMerge
)
target_link_libraries(besq-domain-algorithm PRIVATE
    besq-common-log           # internal logging (optional with BESQ_DISABLE_LOGGER)
)

target_include_directories(besq-domain-algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/algorithm/types/Enchantment.h"
)

# Diagnostics + solution-count macros.  NOTE: these must be defined HERE (this
# is where ExecutionContext/DiagnosticsWriter/strategies are compiled), not
# just on besq-core — a PUBLIC definition on a consumer does NOT reach its
# dependencies, so putting them only on besq-core left them undefined here
# (BESQ_DISABLE_DIAGNOSTICS / BESQ_MAX_SOLUTIONS silently no-ops before).
target_compile_definitions(besq-domain-algorithm PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
    $<$<BOOL:${BESQ_DISABLE_DIAGNOSTICS}>:BESQ_DISABLE_DIAGNOSTICS>
    $<$<BOOL:${BESQ_DEEP_DIAGNOSTICS}>:BESQ_DEEP_DIAGNOSTICS>
)
