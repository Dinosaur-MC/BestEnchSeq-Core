# Strategy source files — auto-discovered via glob.
# Included from algorithm/CMakeLists.txt.

# ── Source files (all .cpp except plugin.cpp) ────────────────────────
file(GLOB BESQ_BUILTIN_STRATEGY_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cpp")
list(FILTER BESQ_BUILTIN_STRATEGY_SRCS EXCLUDE REGEX "/plugin\.cpp$")

# ── Strategy registration — generated code ──────────────────────────
# Discovers class names from *Algorithm.h files in each strategy dir
# and generates a .cpp that directly registers all algorithms (no #ifdef guards).
file(GLOB STRATEGY_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*Algorithm.h")

set(REG_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/_strategy_registration.cpp")
set(REG_CONTENT "// GENERATED — do not edit\n")
set(REG_CONTENT "${REG_CONTENT}#include \"algorithm/strategies/Registration.h\"\n")

foreach(HDR ${STRATEGY_HEADERS})
    file(RELATIVE_PATH INC_PATH "${CMAKE_SOURCE_DIR}/src" "${HDR}")
    string(REPLACE "\\" "/" INC_PATH "${INC_PATH}")
    set(REG_CONTENT "${REG_CONTENT}#include \"${INC_PATH}\"\n")
endforeach()

set(REG_CONTENT "${REG_CONTENT}\nvoid besq_register_builtin_strategies(AlgorithmRegistry &reg) {\n")

foreach(HDR ${STRATEGY_HEADERS})
    get_filename_component(DIR ${HDR} DIRECTORY)
    get_filename_component(DIRNAME ${DIR} NAME)
    get_filename_component(HDRNAME ${HDR} NAME)
    string(REGEX REPLACE "\.h$" "" CLASSNAME ${HDRNAME})
    set(REG_CONTENT "${REG_CONTENT}    reg.register_algorithm(\"${DIRNAME}\", []{ return std::make_unique<${CLASSNAME}>(); });\n")
endforeach()

set(REG_CONTENT "${REG_CONTENT}}\n")
file(WRITE "${REG_SRC}" "${REG_CONTENT}")
list(APPEND BESQ_BUILTIN_STRATEGY_SRCS "${REG_SRC}")
