# ── besq-domain-orchestration ──────────────────────────────────────────────
# Orchestration layer: cross-domain adapters, serializers, formatters.
# Sits between business/algorithm and interface — interface depends on it.

file(GLOB_RECURSE BESQ_ORCHESTRATION_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(besq-domain-orchestration STATIC ${BESQ_ORCHESTRATION_SRCS})

target_link_libraries(besq-domain-orchestration PUBLIC
    besq-domain-algorithm
    besq-domain-business
)

target_include_directories(besq-domain-orchestration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/orchestration/components/CompactAdapter.h"
)
