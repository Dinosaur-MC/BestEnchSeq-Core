# ── besq-domain-algorithm ──────────────────────────────────────────────────
# Algorithm domain: compact types, forge engine, strategies, diagnostics,
# serialization, plugin loader.  Depends only on besq-common.

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
target_link_libraries(besq-domain-algorithm PUBLIC besq-common)

target_include_directories(besq-domain-algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/algorithm/types/Enchantment.h"
)
