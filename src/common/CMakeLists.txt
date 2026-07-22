# ── besq-common ────────────────────────────────────────────────────────────
# Project-neutral infrastructure: I/O primitives, logging, utility types.

set(BESQ_COMMON_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/CommonTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/CsvIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log/Logger.cpp
)

add_library(besq-common STATIC ${BESQ_COMMON_SRCS})

target_include_directories(besq-common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "io/json.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/io/json.h"
)

# Silence MSVC for POSIX-deprecated names used in json.cpp / CsvIO
if(MSVC)
    target_compile_definitions(besq-common PRIVATE _CRT_NONSTDC_NO_DEPRECATE)
endif()
