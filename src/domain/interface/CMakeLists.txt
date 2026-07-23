# ── besq-domain-interface ─────────────────────────────────────────────────
# Interface domain: CLI, API, parsers, ABI bindings.
# Depends on besq-common + besq-domain-algorithm + besq-domain-business.

set(BESQ_INTERFACE_SRCS
    # ── Parsers ──
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/CLIParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/EnchInfoParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/EnchParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/ItemParser.cpp
    # ── CLI ──
    ${CMAKE_CURRENT_SOURCE_DIR}/cli/cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cli/RegistryEditor.cpp
    # ── Core solve pipeline ──
    ${CMAKE_CURRENT_SOURCE_DIR}/api/SolvePipeline.cpp
    # ── Pending API migration ──
    # ${CMAKE_CURRENT_SOURCE_DIR}/api/BesqContext.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/api/CAbiBindings.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/api/ProfileSet.cpp
)

add_library(besq-domain-interface STATIC ${BESQ_INTERFACE_SRCS})

target_link_libraries(besq-domain-interface PUBLIC
    besq-common
    besq-domain-algorithm
    besq-domain-business
)

target_include_directories(besq-domain-interface PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "api/SolvePipeline.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/io/json.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>        # → #include "besq/besq.h"
    ${BESQ_GENERATED_DIR}                                  # → #include "BuildConfig.h"
)

target_compile_definitions(besq-domain-interface PUBLIC
    BESQ_MAX_SOLUTIONS=${BESQ_MAX_SOLUTIONS}
)
