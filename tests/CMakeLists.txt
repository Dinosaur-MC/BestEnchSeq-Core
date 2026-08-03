# ─── Test targets for BestEnchSeq-Core ───
#
# Each test is a standalone executable with its own main() - no test framework.
#
# Tests that exercise project code link the besq-core library and only
# list the test source file.  Truly standalone tests (queues, pools,
# utility headers) stay independent with their own source lists.
#
# Directory layout mirrors the source domain structure:
#   common/       → tests for besq-common (io, utils, config)
#   domain/       → tests per domain:
#     algorithm/    → besq-domain-algorithm
#     business/     → besq-domain-business
#     interface/    → besq-domain-interface
#     orchestration/→ besq-domain-orchestration
#   integration/  → cross-domain integration tests

# Project root (parent of tests/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path for all test targets
# src/common is added so legacy #include "io/json.h" etc. still resolve
set(TEST_INCLUDE
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/common
    ${BESQ_GENERATED_DIR}
)

# So #include "framework/test_utils.h" resolves
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# --- Helpers ---

# PATH extension for MinGW runtime DLLs and besq-core.dll on Windows
set(BESQ_TEST_ENV "PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(BESQ_RUNTIME_DLL_DIR AND EXISTS "${BESQ_RUNTIME_DLL_DIR}/libwinpthread-1.dll")
    set(BESQ_TEST_ENV "${BESQ_TEST_ENV}\\;${BESQ_RUNTIME_DLL_DIR}\\;$ENV{PATH}")
else()
    set(BESQ_TEST_ENV "${BESQ_TEST_ENV}\\;$ENV{PATH}")
endif()

function(besq_test_common_props NAME)
    add_test(NAME ${NAME} COMMAND ${NAME})
    if(BESQ_TEST_ENV)
        set_tests_properties(${NAME} PROPERTIES
            WORKING_DIRECTORY ${PROJECT_ROOT}
            ENVIRONMENT "${BESQ_TEST_ENV}"
        )
    else()
        set_tests_properties(${NAME} PROPERTIES
            WORKING_DIRECTORY ${PROJECT_ROOT}
        )
    endif()
endfunction()

# ── Core link set (DRY) ────────────────────────────────────────────
# business ↔ builtin is a real circular static-lib dependency: business
# parsers call besq::data::* (compiled into besq-core's builtin objects),
# which fill business registries.  GNU ld needs the whole reference chain
# (core → orchestration → business, plus the late entry points) inside
# one --start-group.  Windows lld-link resolves cycles automatically.
if(UNIX)
    set(BESQ_TEST_CORE_LIBS
        -Wl,--start-group
        besq-core besq-domain-business besq-domain-orchestration besq-algo-core
        -Wl,--end-group
    )
    set(BESQ_TEST_CORE_LIBS_INTERFACE
        -Wl,--start-group
        besq-core besq-domain-business besq-domain-orchestration besq-algo-core besq-domain-interface
        -Wl,--end-group
    )
else()
    set(BESQ_TEST_CORE_LIBS besq-core)
    set(BESQ_TEST_CORE_LIBS_INTERFACE besq-core besq-domain-interface)
endif()

# Default: link besq-core for domain tests.
function(besq_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${TEST_INCLUDE}
        ${PROJECT_ROOT}/include
    )
    target_link_libraries(${NAME} PRIVATE ${BESQ_TEST_CORE_LIBS})
    besq_test_common_props(${NAME})
endfunction()

# Variant for interface-domain and integration tests: adds the interface
# domain (inside the group — it is a late entry point to the cycle).
function(besq_test_interface NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${TEST_INCLUDE}
        ${PROJECT_ROOT}/include
    )
    target_link_libraries(${NAME} PRIVATE ${BESQ_TEST_CORE_LIBS_INTERFACE})
    besq_test_common_props(${NAME})
endfunction()

# ─── Subdirectories (per domain) ───
add_subdirectory(common)
add_subdirectory(domain)
add_subdirectory(integration)

# system/ spawns the real besq CLI binary (skipped internally when
# BESQ_BUILD_CLI=OFF — no besq target to invoke).
add_subdirectory(system)
