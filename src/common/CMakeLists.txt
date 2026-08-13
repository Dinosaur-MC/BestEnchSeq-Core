# ═══════════════════════════════════════════════════════════════════════════════
# besq-common — shared infrastructure libraries
# ═══════════════════════════════════════════════════════════════════════════════
#
# Rather than one monolithic static library, common is split into six
# independent sub-targets so consumers link only what they need:
#
#   besq-common-core   NSID, LogLevel, memory pools, lock-free queues,
#                      serialisation interfaces.  (1 .cpp, rest header-only)
#   besq-common-io     JSON parser, CSV I/O, ByteStream
#   besq-common-log    Logger implementation & log.hpp convenience wrappers
#   besq-common-i18n   Language manager, locale detection
#   besq-common-cli    CLIParser v2 (CLICommon, CLIFormatter)
#   besq-common-thread ThreadPool
#
#   besq-common        (INTERFACE) convenience umbrella — links all six.
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
# SHARED: the Logger singleton must be ONE instance per process.  If it were
# static it would be compiled into BOTH besq-algo-core.dll (PRIVATE link) and
# the exe — two Logger objects on Windows, so BESQ_LOG_CONSOLE=0 (configured
# via setup_logger in the exe) would not silence algorithm-domain logs.  A
# shared lib makes every consumer resolve Logger::instance() to the same copy.

add_library(besq-common-log SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/log/Logger.cpp"
)
target_include_directories(besq-common-log PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "log/Logger.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/log/Logger.h"
)
if(WIN32)
    # Export the Logger class so besq-algo-core.dll and besq.exe resolve the
    # SAME instance from this DLL.
    set_property(TARGET besq-common-log PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
# Land next to the host binaries (rpath = build dir / bin) so runtime lookup
# works without LD_LIBRARY_PATH / PATH tricks.
set_target_properties(besq-common-log PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
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
target_link_libraries(besq-common-i18n PUBLIC besq-common-io)

# ── besq-common-thread ────────────────────────────────────────────────────────────

add_library(besq-common-thread STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/thread/ThreadPool.cpp"
)
target_include_directories(besq-common-thread PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "utils/thread/ThreadPool.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/utils/thread/ThreadPool.h"
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
    besq-common-thread
    besq-common-cli
)
target_include_directories(besq-common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
