# plugins/CMakeLists.txt
# Independent build pipeline for algorithm plugin shared libraries.
#
# Requires a pre-built besq-core.  Build with:
#
#   cmake -S plugins -B build/plugins -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugins
#
# Output: build/plugins/algo_<name>.dll (.so)

cmake_minimum_required(VERSION 3.16)
project(BESQ-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Windows: auto-export all symbols so the host can resolve IAlgorithm vtable
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Locate pre-built besq-core (exported via build/besq-coreConfig.cmake)
find_package(besq-core REQUIRED
    HINTS
        "${CMAKE_BINARY_DIR}/.."
        "${BESQ_CORE_DIR}"
    NO_DEFAULT_PATH
)

# Strip include dirs from the imported target — they contain host-platform
# absolute paths (e.g. Windows F:/... when building from WSL) that would
# cause "non-existent path" errors.  Plugins add their own include dirs
# directly below via PLUGIN_SYSTEM_INCLUDES.
set_target_properties(besq-core::besq-core PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
)

# Compute source tree root (parent of plugins/)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Solve-prefix include paths: add src/ and host build/generated/ so plugin code
# can #include "algorithm/IAlgorithm.h" etc. regardless of the host platform.
# Derived relative to the source tree rather than relying on the exported
# target's INTERFACE_INCLUDE_DIRECTORIES, which may contain host-only paths.
set(PLUGIN_SYSTEM_INCLUDES
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/src/common"
)
# Try to locate the host build's generated directory for BuildConfig.h
foreach(_hint ${besq-core_DIR} "${CMAKE_BINARY_DIR}/..")
    if(EXISTS "${_hint}/generated/BuildConfig.h")
        list(APPEND PLUGIN_SYSTEM_INCLUDES "${_hint}/generated")
        break()
    endif()
endforeach()

# Auto-discover all plugin subdirectories
file(GLOB PLUGIN_DIRS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/*/")

foreach(PLUGIN_DIR ${PLUGIN_DIRS})
    file(GLOB PLUGIN_SRCS "${CMAKE_CURRENT_LIST_DIR}/${PLUGIN_DIR}/*.cpp")
    if(NOT PLUGIN_SRCS)
        continue()
    endif()

    get_filename_component(PLUGIN_NAME ${PLUGIN_DIR} NAME)
    add_library(algo_${PLUGIN_NAME} SHARED ${PLUGIN_SRCS})

    target_include_directories(algo_${PLUGIN_NAME} SYSTEM PRIVATE
        ${PLUGIN_SYSTEM_INCLUDES}
    )
    target_include_directories(algo_${PLUGIN_NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"              # for #include "<name>/<file>.h"
    )

    target_link_libraries(algo_${PLUGIN_NAME} PRIVATE besq-core::besq-core)

    set_target_properties(algo_${PLUGIN_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endforeach()
