# ─── System tests: spawn the real besq CLI binary ──────────────────────
# These tests do NOT link besq-core.  They only need the C++ stdlib + the
# spawn helper; the CLI path arrives via compile-time macro + test env.
#
# Guard: BESQ_BIN_PATH target macro needs the besq executable to exist, so
# the whole subdirectory is skipped when BESQ_BUILD_CLI=OFF.
if(NOT BESQ_BUILD_CLI)
    message(STATUS "System tests: skipped (BESQ_BUILD_CLI=OFF — no besq binary)")
    return()
endif()

add_executable(test_system_cli
    test_system_cli.cpp
    # spawn_util.h is header-only — nothing to add here
)

# "framework/test_utils.h" lives one level up (tests/); PROJECT_ROOT is
# tests/CMakeLists.txt's variable (inherited into this subdirectory scope).
# src/ is added so the header-only common/utils/EnvUtil.hpp (get_env) resolves.
target_include_directories(test_system_cli PRIVATE
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/src
)

# ── Binary / project-root as compile-time macros ───────────────────────
# $<TARGET_FILE:besq> canNOT be escaped into a C string literal: on Windows
# the path contains backslashes (\D, \S, \b…) that corrupt the literal.
# Two safe mechanisms:
#   (a) ENVIRONMENT below uses the genex — CTest evaluates it at generate
#       time and env vars do not care about backslashes (authoritative).
#   (b) the compile macro reconstructs the same path at CONFIGURE time and
#       normalizes to forward slashes → no C-string escaping needed.
set(BESQ_SYSTEM_BIN "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/besq${CMAKE_EXECUTABLE_SUFFIX}")
string(REPLACE "\\" "/" BESQ_SYSTEM_BIN_FWD "${BESQ_SYSTEM_BIN}")

set(BESQ_SYSTEM_ROOT_FWD "${PROJECT_ROOT}")
string(REPLACE "\\" "/" BESQ_SYSTEM_ROOT_FWD "${BESQ_SYSTEM_ROOT_FWD}")

target_compile_definitions(test_system_cli PRIVATE
    BESQ_BIN_PATH="${BESQ_SYSTEM_BIN_FWD}"
    BESQ_PROJECT_ROOT="${BESQ_SYSTEM_ROOT_FWD}"
)

# Reuse the existing test registration (add_test + WORKING_DIRECTORY=PROJECT_ROOT
# + PATH for build/bin + MinGW runtime DLLs).
besq_test_common_props(test_system_cli)

# Override ENVIRONMENT to ALSO export the authoritative binary path (genex,
# evaluated by CTest at generate time).  BESQ_TEST_ENV is a ;-separated list of
# VAR=value — merging preserves the PATH entry.  TIMEOUT is a coarse backstop
# for a hung child (the popen harness itself cannot kill).
set_tests_properties(test_system_cli PROPERTIES
    ENVIRONMENT "BESQ_BIN_PATH=$<TARGET_FILE:besq>;${BESQ_TEST_ENV}"
    TIMEOUT 300
)
