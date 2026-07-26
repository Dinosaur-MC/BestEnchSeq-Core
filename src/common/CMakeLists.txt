# ═══════════════════════════════════════════════════════════════════════════════
# besq-common — shared infrastructure libraries
# ═══════════════════════════════════════════════════════════════════════════════
#
# Rather than one monolithic static library, common is split into five
# independent sub-targets so consumers link only what they need:
#
#   besq-common-core   NSID, LogLevel, memory pools, lock-free queues,
#                      serialisation interfaces.  (1 .cpp, rest header-only)
#   besq-common-io     JSON parser, CSV I/O, ByteStream
#   besq-common-log    Logger implementation & log.hpp convenience wrappers
#   besq-common-i18n   Language manager, locale detection
#   besq-common-cli    CLIParser v2 (CLICommon, CLIFormatter)
#
#   besq-common        (INTERFACE) convenience umbrella — links all five.
#
# Each sub-target has BOTH include paths:
#   ${CMAKE_CURRENT_SOURCE_DIR}  → #include "CommonTypes.h" / "io/json.h"
#   ${CMAKE_SOURCE_DIR}/src      → #include "common/CommonTypes.h" / "common/io/json.h"
# so all existing #include styles continue to work regardless of which
# sub-target you link.

# ── besq-common-core ──────────────────────────────────────────────────────────

add_library(besq-common-core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/CommonTypes.cpp"
)
target_include_directories(besq-common-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "CommonTypes.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/CommonTypes.h"
)

# ── besq-common-io ────────────────────────────────────────────────────────────

add_library(besq-common-io STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/io/json.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/CsvIO.cpp"
)
target_include_directories(besq-common-io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "io/json.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/io/json.h"
)
if(MSVC)
    target_compile_definitions(besq-common-io PRIVATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# ── besq-common-log ───────────────────────────────────────────────────────────

add_library(besq-common-log STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/log/Logger.cpp"
)
target_include_directories(besq-common-log PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "log/Logger.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/log/Logger.h"
)

# ── besq-common-i18n ──────────────────────────────────────────────────────────

add_library(besq-common-i18n STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/Language.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/LocaleDetector.cpp"
)
target_include_directories(besq-common-i18n PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "i18n/Language.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/i18n/Language.h"
)

# ── besq-common-cli ───────────────────────────────────────────────────────────

add_library(besq-common-cli STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/cli/CLICommon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/cli/CLIFormatter.cpp"
)
target_include_directories(besq-common-cli PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "utils/cli/CLICommon.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/utils/cli/CLICommon.h"
)

# ── besq-common (convenience umbrella — links all sub-targets) ───────────────

add_library(besq-common INTERFACE)
target_link_libraries(besq-common INTERFACE
    besq-common-core
    besq-common-io
    besq-common-log
    besq-common-i18n
    besq-common-cli
)
target_include_directories(besq-common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
