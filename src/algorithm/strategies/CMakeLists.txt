# Strategy source files — auto-discovered via glob.
# Included from algorithm/CMakeLists.txt.
#
# Each strategy directory should contain its .cpp sources and an optional
# plugin.cpp for shared-library builds.  The main algorithm class header
# is discovered from *Algorithm.h files.

# ── Source files (all .cpp except plugin.cpp) ────────────────────────
file(GLOB BESQ_BUILTIN_STRATEGY_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cpp")
list(FILTER BESQ_BUILTIN_STRATEGY_SRCS EXCLUDE REGEX "/plugin\.cpp$")

# ── Strategy registration — generated code ──────────────────────────
# Discovers class names from *Algorithm.h files in each strategy dir
# and generates a .cpp that includes all headers + registers all algos.
file(GLOB STRATEGY_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*Algorithm.h")

set(REG_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/_strategy_registration.cpp")
set(REG_CONTENT "// GENERATED — do not edit\n")
set(REG_CONTENT "${REG_CONTENT}#include \"registries/AlgorithmRegistry.h\"\n")

set(BESQ_STRATEGY_DEFINES)

foreach(HDR ${STRATEGY_HEADERS})
    get_filename_component(DIR ${HDR} DIRECTORY)
    get_filename_component(DIRNAME ${DIR} NAME)
    get_filename_component(HDRNAME ${HDR} NAME)
    string(REGEX REPLACE "\.h$" "" CLASSNAME ${HDRNAME})

    file(RELATIVE_PATH INC_PATH "${CMAKE_SOURCE_DIR}/src" "${HDR}")
    string(REPLACE "\\" "/" INC_PATH "${INC_PATH}")

    string(TOUPPER "${DIRNAME}" UPPER_DIR)
    string(REGEX REPLACE "[^A-Z0-9]" "_" DEFINE "BESQ_HAVE_${UPPER_DIR}")

    set(REG_CONTENT "${REG_CONTENT}#ifdef ${DEFINE}\n")
    set(REG_CONTENT "${REG_CONTENT}#  include \"${INC_PATH}\"\n")
    set(REG_CONTENT "${REG_CONTENT}#endif\n")

    list(APPEND BESQ_STRATEGY_DEFINES "${DEFINE}")
endforeach()

set(REG_CONTENT "${REG_CONTENT}\nvoid besq_register_builtin_strategies(AlgorithmRegistry &reg) {\n")

foreach(HDR ${STRATEGY_HEADERS})
    get_filename_component(DIR ${HDR} DIRECTORY)
    get_filename_component(DIRNAME ${DIR} NAME)
    get_filename_component(HDRNAME ${HDR} NAME)
    string(REGEX REPLACE "\.h$" "" CLASSNAME ${HDRNAME})

    string(TOUPPER "${DIRNAME}" UPPER_DIR)
    string(REGEX REPLACE "[^A-Z0-9]" "_" DEFINE "BESQ_HAVE_${UPPER_DIR}")

    set(REG_CONTENT "${REG_CONTENT}#ifdef ${DEFINE}\n")
    set(REG_CONTENT "${REG_CONTENT}    reg.register_algorithm(\"${DIRNAME}\", []{ return std::make_unique<${CLASSNAME}>(); });\n")
    set(REG_CONTENT "${REG_CONTENT}#endif\n")
endforeach()
set(REG_CONTENT "${REG_CONTENT}}\n")

file(WRITE "${REG_SRC}" "${REG_CONTENT}")
set(BESQ_ALGO_REG_SRC "${REG_SRC}")

# ── Exported compile definitions ────────────────────────────────────
set(BESQ_STRATEGY_DEFINITIONS ${BESQ_STRATEGY_DEFINES})
