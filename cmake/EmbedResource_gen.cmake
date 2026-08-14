# EmbedResource_gen.cmake
#
# Build/configure-time generator for the embedded-resource access layer.
# Called by EmbedResource.cmake's besq_embed_resources() — never by hand.

# Script mode (`cmake -P`) has NO cmake_minimum_required/project() context, so
# policies fall back to OLD on CMake < 4.0 — without this, `IN_LIST` (CMP0057,
# NEW since 3.3) fails with "Unknown arguments".  The project requires
# CMake >= 3.25, so NEW is always safe here.
if(POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif()
#
# Two modes:
#
# 1. Shared header (configure time):
#      cmake -DGEN_HEADER=1 -DHEADER=<path>
#            -DGROUPS="g1;g2" -DALL_MEMBERS="m1;m2"
#            -DGROUP_MAP="m1=g1;m2=g2"
#            -P EmbedResource_gen.cmake
#    Writes the single global enum (ResourceId) + inline raw()/
#    resource_name()/group_of().  raw() is an INLINE dispatcher: each member
#    routes to its group's private accessor (detail::<group>_raw), which is
#    defined out-of-line in the group implementation.  This keeps the
#    interface uniform while letting each group's .cpp carry only its own
#    data — and avoids the duplicate-symbol trap of defining raw() twice
#    (static linking would silently keep one definition and the other
#    group's resources would read empty).
#    Idempotent; the last caller's run sees the full member set accumulated
#    across all besq_embed_resources() calls.
#
# 2. Group implementation (build time, per group):
#      cmake -DGROUP=<g> -DOUTPUT=<path>
#            -DMEMBERS="m1=path;m2=path" -DGROUP_MEMBERS="m1;m2"
#            -P EmbedResource_gen.cmake
#    Writes one .cpp with constexpr byte arrays for the group's resources and
#    the out-of-line definition of detail::<group>_raw(): own-group members
#    return their data, everything else returns an empty view.  The inline
#    dispatcher in the header is what keeps cross-group access defined.

string(ASCII 10 _NL)

# ────────────────────────────────────────────────────────────────────────────
# Mode 1: shared header
# ────────────────────────────────────────────────────────────────────────────
if(GEN_HEADER)
    if(NOT HEADER)
        message(FATAL_ERROR "EmbedResource_gen: GEN_HEADER=1 requires HEADER")
    endif()
    if(NOT GROUP_MAP)
        message(FATAL_ERROR "EmbedResource_gen: GEN_HEADER=1 requires GROUP_MAP (member=group;...)")
    endif()

    # member -> group lookup helper inline (script-local function)
    function(besq_embed_member_group member out_var)
        foreach(_pair IN LISTS GROUP_MAP)
            string(FIND "${_pair}" "=" _eq)
            if(_eq LESS 0)
                continue()
            endif()
            string(SUBSTRING "${_pair}" 0 ${_eq} _pm)
            if(_pm STREQUAL "${member}")
                math(EXPR _eq1 "${_eq} + 1")
                string(SUBSTRING "${_pair}" ${_eq1} -1 _pg)
                set(${out_var} "${_pg}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        message(FATAL_ERROR "EmbedResource_gen: member '${member}' missing from GROUP_MAP")
    endfunction()

    # The full group set comes from GROUP_MAP — the header is re-emitted on
    # every besq_embed_resources() call and the LAST call's GROUPS argument
    # would only cover that call's groups, silently dropping earlier groups
    # from the dispatch switch.
    set(_all_groups "")
    foreach(_pair IN LISTS GROUP_MAP)
        string(FIND "${_pair}" "=" _eq)
        if(_eq LESS 0)
            continue()
        endif()
        math(EXPR _eq1 "${_eq} + 1")
        string(SUBSTRING "${_pair}" ${_eq1} -1 _pg)
        if(NOT "${_pg}" IN_LIST _all_groups)
            list(APPEND _all_groups "${_pg}")
        endif()
    endforeach()

    set(_enum "")
    foreach(_m IN LISTS ALL_MEMBERS)
        string(APPEND _enum "    ${_m},${_NL}")
    endforeach()
    string(APPEND _enum "    COUNT${_NL}")

    # detail accessor declarations: one per group
    set(_detail_decls "")
    foreach(_g IN LISTS _all_groups)
        string(APPEND _detail_decls
            "/// Out-of-line per-group accessor — defined in the ${_g} group${_NL}"
            "/// implementation (builtin/${_g}_assets.cpp).  Own-group members${_NL}"
            "/// return their data; any other member returns an empty view.${_NL}"
            "std::string_view ${_g}_raw(ResourceId id) noexcept;${_NL}")
    endforeach()

    # raw() dispatcher: group each member's case under its group accessor.
    # No `default` branch: -Wswitch forces a case for every enumerator.
    set(_group_cases "")
    foreach(_g IN LISTS _all_groups)
        set(_g_members "")
        foreach(_m IN LISTS ALL_MEMBERS)
            besq_embed_member_group("${_m}" _mg)
            if(_mg STREQUAL "${_g}")
                list(APPEND _g_members "${_m}")
            endif()
        endforeach()
        if(_g_members)
            foreach(_m IN LISTS _g_members)
                string(APPEND _group_cases "        case ResourceId::${_m}:${_NL}")
            endforeach()
            string(APPEND _group_cases "            return detail::${_g}_raw(id);${_NL}")
        endif()
    endforeach()
    string(APPEND _group_cases "        case ResourceId::COUNT:${_NL}")
    string(APPEND _group_cases "            return {};${_NL}")

    # resource_name()/group_of(): fully inline (pure string switches)
    set(_name_cases "")
    foreach(_m IN LISTS ALL_MEMBERS)
        string(APPEND _name_cases "        case ResourceId::${_m}: return \"${_m}\";${_NL}")
    endforeach()
    string(APPEND _name_cases "        case ResourceId::COUNT: return \"COUNT\";${_NL}")

    set(_groupof_cases "")
    foreach(_m IN LISTS ALL_MEMBERS)
        besq_embed_member_group("${_m}" _mg)
        string(APPEND _groupof_cases "        case ResourceId::${_m}: return \"${_mg}\";${_NL}")
    endforeach()
    string(APPEND _groupof_cases "        case ResourceId::COUNT: return \"\";${_NL}")

    file(WRITE "${HEADER}"
        "// Auto-generated by EmbedResource.cmake — do not edit.${_NL}"
        "// Single source of truth: the besq_embed_resources() declarations in${_NL}"
        "// CMakeLists.txt.  To add a resource, edit that declaration and${_NL}"
        "// reconfigure — the generator re-emits this header and the group${_NL}"
        "// implementations automatically.${_NL}"
        "#pragma once${_NL}"
        "#include <string_view>${_NL}"
        "${_NL}"
        "namespace besq::data {${_NL}"
        "${_NL}"
        "/// Embedded-resource ids.  Members carry their group prefix${_NL}"
        "/// (data_ / frontend_ / ...); the generator rejects duplicate or${_NL}"
        "/// ungrouped names at configure time.${_NL}"
        "enum class ResourceId {${_NL}"
        "${_enum}"
        "};${_NL}"
        "${_NL}"
        "namespace detail {${_NL}"
        "${_detail_decls}"
        "} // namespace detail${_NL}"
        "${_NL}"
        "/// Raw read-only access to the embedded resource bytes.  Inline${_NL}"
        "/// dispatcher: each member routes to its group's out-of-line${_NL}"
        "/// accessor, so every TU sees the full switch (compiler checks${_NL}"
        "/// coverage) while the data lives in one group implementation.${_NL}"
        "inline std::string_view raw(ResourceId id) noexcept {${_NL}"
        "    switch (id) {${_NL}"
        "${_group_cases}"
        "    }${_NL}"
        "}${_NL}"
        "${_NL}"
        "/// Enum member name (\"data_vanilla_json\") — diagnostics/logging.${_NL}"
        "inline std::string_view resource_name(ResourceId id) noexcept {${_NL}"
        "    switch (id) {${_NL}"
        "${_name_cases}"
        "    }${_NL}"
        "}${_NL}"
        "${_NL}"
        "/// Group name (\"data\", \"frontend\", ...) — diagnostics/logging.${_NL}"
        "inline std::string_view group_of(ResourceId id) noexcept {${_NL}"
        "    switch (id) {${_NL}"
        "${_groupof_cases}"
        "    }${_NL}"
        "}${_NL}"
        "${_NL}"
        "} // namespace besq::data${_NL}"
    )
    return()
endif()

# ────────────────────────────────────────────────────────────────────────────
# Mode 2: group implementation
# ────────────────────────────────────────────────────────────────────────────
if(NOT GROUP OR NOT OUTPUT OR NOT MEMBERS OR NOT GROUP_MEMBERS)
    message(FATAL_ERROR "EmbedResource_gen: group mode requires GROUP/OUTPUT/MEMBERS/GROUP_MEMBERS")
endif()

# --- byte arrays for this group's resources ---------------------------------
set(_arrays "")
foreach(_r IN LISTS MEMBERS)
    string(FIND "${_r}" "=" _eq)
    if(_eq LESS 0)
        message(FATAL_ERROR "EmbedResource_gen: bad member entry '${_r}' (expected <member>=<path>)")
    endif()
    string(SUBSTRING "${_r}" 0 ${_eq} _member)
    math(EXPR _eq1 "${_eq} + 1")
    string(SUBSTRING "${_r}" ${_eq1} -1 _path)

    file(READ "${_path}" _hex HEX)
    string(LENGTH "${_hex}" _hex_len)
    math(EXPR _data_len "${_hex_len} / 2")

    set(_bytes "")
    set(_i 0)
    set(_per_line 16)
    while(_i LESS _hex_len)
        set(_line "")
        set(_sep "")
        set(_j 0)
        while(_j LESS _per_line AND _i LESS _hex_len)
            string(SUBSTRING "${_hex}" ${_i} 2 _byte)
            string(APPEND _line "${_sep}0x${_byte}")
            set(_sep ", ")
            math(EXPR _i "${_i} + 2")
            math(EXPR _j "${_j} + 1")
        endwhile()
        string(APPEND _bytes "    ${_line},${_NL}")
    endwhile()

    string(APPEND _arrays
        "constexpr std::array<unsigned char, ${_data_len}> k${_member} = {${_NL}${_bytes}};${_NL}${_NL}")
endforeach()

# --- detail::<group>_raw(): own-group members return data, others {} --------
set(_raw_cases "")
foreach(_m IN LISTS GROUP_MEMBERS)
    string(APPEND _raw_cases
        "        case ResourceId::${_m}:${_NL}"
        "            return std::string_view(reinterpret_cast<const char*>(k${_m}.data()), k${_m}.size());${_NL}")
endforeach()

file(WRITE "${OUTPUT}"
    "// Auto-generated by EmbedResource.cmake — do not edit.${_NL}"
    "// Group \"${GROUP}\" — compiled into the owning target.${_NL}"
    "#include \"builtin/EmbeddedResources_generated.h\"${_NL}"
    "${_NL}"
    "#include <array>${_NL}"
    "#include <string_view>${_NL}"
    "${_NL}"
    "namespace besq::data {${_NL}"
    "namespace {${_NL}"
    "${_arrays}"
    "} // namespace${_NL}"
    "${_NL}"
    "namespace detail {${_NL}"
    "${_NL}"
    "std::string_view ${GROUP}_raw(ResourceId id) noexcept {${_NL}"
    "    switch (id) {${_NL}"
    "${_raw_cases}"
    "        default:${_NL}"
    "            return {};${_NL}"
    "    }${_NL}"
    "}${_NL}"
    "${_NL}"
    "} // namespace detail${_NL}"
    "} // namespace besq::data${_NL}"
)
