# ── besq-orchestration ─────────────────────────────────────────────────────
# Orchestration layer: cross-domain adapters, serializers, formatters.
# Links all domains transitively.

file(GLOB_RECURSE BESQ_ORCHESTRATION_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(besq-orchestration STATIC ${BESQ_ORCHESTRATION_SRCS})

target_link_libraries(besq-orchestration PUBLIC
    besq-domain-interface   # transitively pulls in algorithm + business + common
)

target_include_directories(besq-orchestration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/orchestration/components/CompactAdapter.h"
)
