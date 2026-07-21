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

# Locate pre-built besq-core
find_package(besq-core REQUIRED
    HINTS
        "${CMAKE_CURRENT_LIST_DIR}/../build"
        "${BESQ_CORE_DIR}"
    NO_DEFAULT_PATH
)

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

    target_include_directories(algo_${PLUGIN_NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"              # for #include "<name>/<file>.h"
    )

    target_link_libraries(algo_${PLUGIN_NAME} PRIVATE besq-core::besq-core)

    set_target_properties(algo_${PLUGIN_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endforeach()
