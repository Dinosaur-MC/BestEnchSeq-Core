# ── besq-orchestration ─────────────────────────────────────────────────────
# Orchestration layer: cross-domain adapters, serializers, formatters.
# Links all domains transitively.

set(BESQ_ORCHESTRATION_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/components/CompactAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/components/EnchSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/components/OutputFormatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/components/RawTypeAdapter.cpp
)

add_library(besq-orchestration STATIC ${BESQ_ORCHESTRATION_SRCS})

target_link_libraries(besq-orchestration PUBLIC
    besq-domain-interface   # transitively pulls in algorithm + business + common
)

target_include_directories(besq-orchestration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/orchestration/components/CompactAdapter.h"
)
