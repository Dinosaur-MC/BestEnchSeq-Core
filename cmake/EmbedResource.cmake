# EmbedResource.cmake
#
# besq_embed_resources(
#     GROUPS <group> <target> [<group> <target> ...]
#     RESOURCES <member>=<path> [<member>=<path> ...]
# )
#
# Declares the embedded-resource access layer for a set of resource groups.
# This declaration is the SINGLE SOURCE OF TRUTH — everything else is
# generated:
#
#   - Shared header  ${BESQ_GENERATED_DIR}/builtin/EmbeddedResources_generated.h
#     (configure time, idempotent; the last call sees the full member set):
#       enum class ResourceId { <all members, group-prefixed>, COUNT };
#       constexpr raw() / resource_name() / group_of() declarations.
#
#   - One implementation per group, ${BESQ_GENERATED_DIR}/builtin/<group>_assets.cpp
#     (build time, DEPENDS on the group's resource files + shared header):
#       constexpr byte arrays + switch-coverage implementations.  Own-group
#       members return their data; other groups' members return an empty
#       view.  No `default` branch, so -Wswitch flags any enumerator the
#       implementation does not cover.
#
# Member names MUST start with their group name + "_" (e.g. data_vanilla_json
# for group "data").  The generator validates at configure time:
#   - member name syntax ([a-zA-Z_][a-zA-Z0-9_]*)
#   - resource file exists and is non-empty
#   - the member's group prefix matches one declared group (unambiguous)
#   - no duplicate member name across all besq_embed_resources() calls
#
# The generated .cpp is added to the group's target via target_sources().

include(CMakeParseArguments)

# Resolved inside besq_embed_resources() (CMAKE_CURRENT_FUNCTION_LIST_DIR is
# only defined in a function scope).

function(besq_embed_resources)
    set(BESQ_EMBEDDED_GEN_SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedResource_gen.cmake")
    set(BESQ_EMBEDDED_HEADER "${BESQ_GENERATED_DIR}/builtin/EmbeddedResources_generated.h")
    # PARSE_ARGV signature: the plain `... <args>` form mis-parses when the
    # options/one_value lists are empty strings on CMake >= 4.0 (empty args
    # get folded and shift the keyword slots); PARSE_ARGV is unambiguous.
    cmake_parse_arguments(PARSE_ARGV 0 ER "" "" "GROUPS;RESOURCES")

    if(NOT ER_GROUPS)
        message(FATAL_ERROR "besq_embed_resources: GROUPS is required (<group> <target> pairs)")
    endif()
    if(NOT ER_RESOURCES)
        message(FATAL_ERROR "besq_embed_resources: RESOURCES is required (<member>=<path> entries)")
    endif()
    if(ER_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "besq_embed_resources: unexpected arguments: ${ER_UNPARSED_ARGUMENTS}")
    endif()

    # ── 1. Parse GROUPS: <group> <target> pairs ────────────────────────────
    list(LENGTH ER_GROUPS _glen)
    math(EXPR _gmod "${_glen} % 2")
    if(NOT _gmod EQUAL 0)
        message(FATAL_ERROR "besq_embed_resources: GROUPS must be <group> <target> pairs (odd count)")
    endif()
    set(_groups "")
    set(_idx 0)
    while(_idx LESS _glen)
        list(GET ER_GROUPS ${_idx} _g)
        math(EXPR _idx1 "${_idx} + 1")
        list(GET ER_GROUPS ${_idx1} _t)
        if(NOT TARGET "${_t}")
            message(FATAL_ERROR "besq_embed_resources: unknown CMake target '${_t}' for group '${_g}'")
        endif()
        list(APPEND _groups "${_g}")
        set(BESQ_EMBEDDED_TARGET_${_g} "${_t}")
        math(EXPR _idx "${_idx} + 2")
    endwhile()

    # ── 2. Parse RESOURCES: <member>=<path> with validation ────────────────
    foreach(_r IN LISTS ER_RESOURCES)
        string(FIND "${_r}" "=" _eq)
        if(_eq LESS 0)
            message(FATAL_ERROR "besq_embed_resources: RESOURCES entry must be <member>=<path>: '${_r}'")
        endif()
        string(SUBSTRING "${_r}" 0 ${_eq} _member)
        math(EXPR _eq1 "${_eq} + 1")
        string(SUBSTRING "${_r}" ${_eq1} -1 _path)

        # member name syntax
        string(REGEX MATCH "^[a-zA-Z_][a-zA-Z0-9_]*$" _ok "${_member}")
        if(NOT _ok)
            message(FATAL_ERROR "besq_embed_resources: invalid member name '${_member}' (expected [a-zA-Z_][a-zA-Z0-9_]*)")
        endif()
        # resource file exists + non-empty
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR "besq_embed_resources: resource file not found: ${_path}")
        endif()
        file(SIZE "${_path}" _sz)
        if(_sz EQUAL 0)
            message(FATAL_ERROR "besq_embed_resources: empty resource file: ${_path}")
        endif()
        # group prefix must match exactly one declared group
        set(_owner "")
        foreach(_g IN LISTS _groups)
            string(LENGTH "${_g}" _gl)
            string(SUBSTRING "${_member}" 0 ${_gl} _pre)
            if(_pre STREQUAL "${_g}")
                if(_owner)
                    message(FATAL_ERROR "besq_embed_resources: member '${_member}' matches multiple group prefixes (${_owner}, ${_g})")
                endif()
                set(_owner "${_g}")
            endif()
        endforeach()
        if(NOT _owner)
            message(FATAL_ERROR "besq_embed_resources: member '${_member}' has no matching group prefix in (${_groups})")
        endif()
        # duplicate member across all calls (re-read the registry inside the
        # loop so duplicates WITHIN this call are caught too)
        get_property(_registered DIRECTORY PROPERTY BESQ_EMBEDDED_MEMBERS)
        if("${_member}" IN_LIST _registered)
            message(FATAL_ERROR "besq_embed_resources: duplicate resource member '${_member}'")
        endif()

        set_property(DIRECTORY APPEND PROPERTY BESQ_EMBEDDED_MEMBERS "${_member}")
        set_property(DIRECTORY APPEND PROPERTY BESQ_EMBEDDED_PATH_${_member} "${_path}")
        set_property(DIRECTORY APPEND PROPERTY BESQ_EMBEDDED_GROUP_${_member} "${_owner}")

    endforeach()

    # ── 3. Shared header (configure time, idempotent) ──────────────────────
    # Re-emitted on every call so the LAST call's run sees the complete
    # member set accumulated across all besq_embed_resources() declarations.
    get_property(_all_members DIRECTORY PROPERTY BESQ_EMBEDDED_MEMBERS)

    # member -> group map for the inline dispatcher (all members)
    set(_group_map "")
    foreach(_m IN LISTS _all_members)
        get_property(_owner DIRECTORY PROPERTY BESQ_EMBEDDED_GROUP_${_m})
        list(APPEND _group_map "${_m}=${_owner}")
    endforeach()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DGEN_HEADER=1"
            "-DHEADER=${BESQ_EMBEDDED_HEADER}"
            "-DGROUPS=${_groups}"
            "-DALL_MEMBERS=${_all_members}"
            "-DGROUP_MAP=${_group_map}"
            -P "${BESQ_EMBEDDED_GEN_SCRIPT}"
        RESULT_VARIABLE _gen_res
    )
    if(NOT _gen_res EQUAL 0)
        message(FATAL_ERROR "besq_embed_resources: shared header generation failed (${_gen_res})")
    endif()

    # ── 4. Group implementations (build time, per group) ───────────────────
    foreach(_g IN LISTS _groups)
        # collect this group's members + paths (cross-call view)
        set(_g_members "")
        set(_g_paths "")
        foreach(_m IN LISTS _all_members)
            get_property(_owner DIRECTORY PROPERTY BESQ_EMBEDDED_GROUP_${_m})
            if(_owner STREQUAL "${_g}")
                get_property(_p DIRECTORY PROPERTY BESQ_EMBEDDED_PATH_${_m})
                list(APPEND _g_members "${_m}=${_p}")
                list(APPEND _g_paths "${_p}")
            endif()
        endforeach()

        # member names owned by THIS group (subset for switch generation)
        set(_own_members "")
        foreach(_m IN LISTS _all_members)
            get_property(_owner DIRECTORY PROPERTY BESQ_EMBEDDED_GROUP_${_m})
            if(_owner STREQUAL "${_g}")
                list(APPEND _own_members "${_m}")
            endif()
        endforeach()

        set(_out "${BESQ_GENERATED_DIR}/builtin/${_g}_assets.cpp")
        add_custom_command(
            OUTPUT  "${_out}"
            DEPENDS ${_g_paths} "${BESQ_EMBEDDED_HEADER}" "${BESQ_EMBEDDED_GEN_SCRIPT}"
            COMMAND "${CMAKE_COMMAND}"
                "-DGROUP=${_g}"
                "-DOUTPUT=${_out}"
                "-DMEMBERS=${_g_members}"
                "-DGROUP_MEMBERS=${_own_members}"
                -P "${BESQ_EMBEDDED_GEN_SCRIPT}"
            COMMENT "Generating embedded resource implementation: ${_g}_assets.cpp"
        )
        set_source_files_properties("${_out}" PROPERTIES GENERATED TRUE)

        set(_tgt "${BESQ_EMBEDDED_TARGET_${_g}}")
        target_sources("${_tgt}" PRIVATE "${_out}")
        # Cross-directory bridge: CMake's Ninja generator does not associate a
        # custom command registered in a PARENT directory with a source file of
        # a target defined in a SUBDIRECTORY (verified on CMake 4.0).  Force
        # the ordering explicitly via a custom target so the generated .cpp
        # always exists before it is compiled.
        set(_gen_target "besq_embed_gen_${_g}")
        add_custom_target("${_gen_target}" DEPENDS "${_out}")
        add_dependencies("${_tgt}" "${_gen_target}")
        message(STATUS "besq_embed_resources: group '${_g}' (${_own_members} members) → ${_out}")
    endforeach()
endfunction()
