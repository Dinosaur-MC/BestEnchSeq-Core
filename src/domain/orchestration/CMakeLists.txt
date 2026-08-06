# ── besq-domain-orchestration ──────────────────────────────────────────────
# Orchestration layer: cross-domain adapters, serializers, formatters.
# Sits between business/algorithm and interface — interface depends on it.

file(GLOB_RECURSE BESQ_ORCHESTRATION_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(besq-domain-orchestration STATIC ${BESQ_ORCHESTRATION_SRCS})

target_link_libraries(besq-domain-orchestration PUBLIC
    besq-algo-core          # single shared source of the algorithm domain
    besq-domain-business
)
target_link_libraries(besq-domain-orchestration PRIVATE
    besq-common-log           # internal logging (not propagated)
    besq-common-i18n          # tr() / LanguageManager in OutputFormatter
)

target_include_directories(besq-domain-orchestration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/orchestration/components/CompactAdapter.h"
)
