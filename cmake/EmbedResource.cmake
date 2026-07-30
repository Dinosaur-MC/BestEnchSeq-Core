# EmbedResource.cmake
#
# embed_resource(INPUT <path> [SYMBOL <name>] [OUTPUT <cpp_file>])
#
# Registers a build-level custom command that embeds a file's contents as
# a C++ constexpr std::array<unsigned char, N>.  The generated file is
# automatically re-generated when INPUT changes.
#
# The generated file exposes besq::data::<SYMBOL>() -> std::string_view.
# SYMBOL defaults to the stem of INPUT (e.g. "vanilla" → "vanilla").
# OUTPUT defaults to ${PROJECT_BINARY_DIR}/generated/<SYMBOL>.cpp.

include(CMakeParseArguments)

function(embed_resource)
    cmake_parse_arguments(ER "" "INPUT;SYMBOL;OUTPUT" "" ${ARGN})

    if(NOT ER_INPUT)
        message(FATAL_ERROR "embed_resource: INPUT is required")
    endif()
    if(NOT EXISTS "${ER_INPUT}")
        message(FATAL_ERROR "embed_resource: file not found: ${ER_INPUT}")
    endif()

    # Derive SYMBOL from filename if not provided
    if(NOT ER_SYMBOL)
        get_filename_component(_stem "${ER_INPUT}" NAME_WE)
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" ER_SYMBOL "${_stem}")
    endif()

    # Default OUTPUT
    if(NOT ER_OUTPUT)
        set(ER_OUTPUT "${PROJECT_BINARY_DIR}/generated/${ER_SYMBOL}.cpp")
    endif()

    # Generate output via a build-time custom command with dependency tracking.
    # When INPUT changes, the build system re-runs the script automatically.
    set(_gen_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedResource_gen.cmake")
    add_custom_command(
        OUTPUT  "${ER_OUTPUT}"
        DEPENDS "${ER_INPUT}" "${_gen_script}"
        COMMAND "${CMAKE_COMMAND}"
            -DINPUT="${ER_INPUT}"
            -DOUTPUT="${ER_OUTPUT}"
            -DSYMBOL="${ER_SYMBOL}"
            -P "${_gen_script}"
        COMMENT "Generating embedded resource: ${ER_SYMBOL}"
    )

    # Mark the output as generated (tells CMake not to check existence at
    # configure time; the custom command creates it at build time).
    set_source_files_properties("${ER_OUTPUT}" PROPERTIES GENERATED TRUE)

    # Print size info at configure time
    file(SIZE "${ER_INPUT}" _size)
    message(STATUS "embed_resource: ${ER_SYMBOL} (${_size} bytes) → ${ER_OUTPUT}")
endfunction()
