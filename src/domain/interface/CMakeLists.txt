# ── besq-http — reusable HTTP framework (components/http) ──────────────────
# Open reusable capability unit. Excluded from besq-domain-interface below so
# the sources are not compiled twice.
file(GLOB_RECURSE BESQ_HTTP_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/http/*.cpp"
)
add_library(besq-http STATIC ${BESQ_HTTP_SRCS})
target_link_libraries(besq-http PUBLIC besq-common-core PRIVATE besq-common-io besq-common-log)
target_include_directories(besq-http PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
if(WIN32)
    target_link_libraries(besq-http PRIVATE ws2_32)
endif()

# ── besq-domain-interface ─────────────────────────────────────────────────
# Interface domain: session context + CLI + ABI bindings.
# Depends on orchestration (which brings algorithm + business transitively)
# plus common sub-targets for i18n, CLI parsing, and logging.
file(GLOB_RECURSE BESQ_INTERFACE_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
# components/http lives in besq-http — drop those .cpp files so each
# translation unit is compiled exactly once.
list(REMOVE_ITEM BESQ_INTERFACE_SRCS ${BESQ_HTTP_SRCS})

add_library(besq-domain-interface STATIC ${BESQ_INTERFACE_SRCS})

target_link_libraries(besq-domain-interface
    PUBLIC  besq-domain-orchestration    # SolveRequest / SolveResult in BesqContext.h API
    PRIVATE besq-http                    # reusable HTTP framework (components/http)
    PRIVATE besq-common-core             # NSID (transitive via orchestration, explicit for safety)
    PRIVATE besq-common-i18n             # Language / LocaleDetector for CLIApp
    PRIVATE besq-common-cli              # CLIParser v2 for CLIApp
    PRIVATE besq-common-log              # internal logging
)

target_include_directories(besq-domain-interface PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/interface/SolvePipeline.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>        # → #include "besq/besq.h"
    ${BESQ_GENERATED_DIR}                                  # → #include "BuildConfig.h"
)

target_compile_definitions(besq-domain-interface PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
)

if(WIN32)
    target_link_libraries(besq-domain-interface PRIVATE ws2_32)
endif()
