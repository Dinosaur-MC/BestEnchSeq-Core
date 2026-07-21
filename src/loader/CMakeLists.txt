# Algorithm Loader source group
# Included from root CMakeLists.txt via include()

set(BESQ_LOADER_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/AlgorithmLoader.cpp"
)

# ── Plugin shared libraries (optional — -DBESQ_BUILD_PLUGINS=ON) ─────
# Each plugin is a standalone .so/.dll that AlgorithmLoader can load at
# runtime.  When enabled, besq-core is automatically built as a SHARED
# library so that host and plugins share the same heap + vtables.
#
# Usage:
#   cmake -B build -DBESQ_BUILD_PLUGINS=ON
#   cmake --build build
#   build/bin/besq --algo-dir build/bin/algorithms --list-algorithms

option(BESQ_BUILD_PLUGINS "Build algorithm plugins as shared libraries" OFF)

if(BESQ_BUILD_PLUGINS)
    # Auto-discover all strategy plugin.cpp files via glob.
    # To add a new plugin, just create plugin.cpp in the strategy's directory.
    file(GLOB PLUGIN_SRCS RELATIVE "${CMAKE_SOURCE_DIR}"
        "src/algorithm/strategies/*/plugin.cpp")
    foreach(PLUGIN_SRC ${PLUGIN_SRCS})
        get_filename_component(DIR ${PLUGIN_SRC} DIRECTORY)
        get_filename_component(NAME ${DIR} NAME)
        add_library(algo_${NAME} SHARED "${CMAKE_SOURCE_DIR}/${PLUGIN_SRC}")
        target_include_directories(algo_${NAME} PRIVATE ${BESQ_PUBLIC_INCLUDES})
        target_link_libraries(algo_${NAME} PRIVATE besq-minimal)
        set_target_properties(algo_${NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/algorithms"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/algorithms"
        )
    endforeach()
endif()
