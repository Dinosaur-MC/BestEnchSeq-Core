# ── besq-domain-interface ─────────────────────────────────────────────────
# Interface domain: session context + CLI + ABI bindings.
# Depends on besq-common + besq-domain-algorithm + besq-domain-business.

file(GLOB_RECURSE BESQ_INTERFACE_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(besq-domain-interface STATIC ${BESQ_INTERFACE_SRCS})

target_link_libraries(besq-domain-interface PUBLIC
    besq-common
    besq-domain-algorithm
    besq-domain-business
)

target_include_directories(besq-domain-interface PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/interface/SolvePipeline.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>        # → #include "besq/besq.h"
    ${BESQ_GENERATED_DIR}                                  # → #include "BuildConfig.h"
)

target_compile_definitions(besq-domain-interface PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
)
