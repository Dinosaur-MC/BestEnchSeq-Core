# ── besq-domain-algorithm ──────────────────────────────────────────────────
# Algorithm domain: compact types, forge engine, strategies, diagnostics,
# serialization, plugin loader.  Depends only on besq-common.

set(BESQ_ALGORITHM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/AlgorithmExecutor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExecutionContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/forge_engine/ForgeEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/AlgorithmDiagnostics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/DiagnosticsService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/DiagnosticsWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/serialization/CompactSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/serialization/IAlgorithmSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/AlgorithmRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/EnchReg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/AlgorithmLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Enchantment.cpp
    # ── Resolvers ──
    ${CMAKE_CURRENT_SOURCE_DIR}/resolvers/ItemResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resolvers/InventoryResolver.cpp
)

# ── Strategy auto-discovery ────────────────────────────────────────────────
file(GLOB BESQ_BUILTIN_STRATEGY_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/strategies/*/*.cpp")
list(FILTER BESQ_BUILTIN_STRATEGY_SRCS EXCLUDE REGEX "/plugin\.cpp$")

# Generate registration code (classes are now in namespace algorithm)
file(GLOB STRATEGY_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/strategies/*/*Algorithm.h")

set(REG_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/_algorithm_strategy_registration.cpp")
set(REG_CONTENT "// GENERATED — do not edit\n")
set(REG_CONTENT "${REG_CONTENT}#include \"domain/algorithm/strategies/Registration.h\"\n")

foreach(HDR ${STRATEGY_HEADERS})
    file(RELATIVE_PATH INC_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${HDR}")
    string(REPLACE "\\" "/" INC_PATH "${INC_PATH}")
    set(REG_CONTENT "${REG_CONTENT}#include \"domain/algorithm/${INC_PATH}\"\n")
endforeach()

set(REG_CONTENT "${REG_CONTENT}\nvoid besq_register_builtin_strategies(algorithm::AlgorithmRegistry &reg) {\n")

foreach(HDR ${STRATEGY_HEADERS})
    get_filename_component(DIR ${HDR} DIRECTORY)
    get_filename_component(DIRNAME ${DIR} NAME)
    get_filename_component(HDRNAME ${HDR} NAME)
    string(REGEX REPLACE "\.h$" "" CLASSNAME ${HDRNAME})
    # Strategy classes are now in namespace algorithm
    set(REG_CONTENT "${REG_CONTENT}    reg.register_algorithm(\"${DIRNAME}\", []{ return std::make_unique<algorithm::${CLASSNAME}>(); });\n")
endforeach()

set(REG_CONTENT "${REG_CONTENT}}\n")
file(WRITE "${REG_SRC}" "${REG_CONTENT}")
list(APPEND BESQ_BUILTIN_STRATEGY_SRCS "${REG_SRC}")

# ── Library target ─────────────────────────────────────────────────────────
add_library(besq-domain-algorithm STATIC
    ${BESQ_ALGORITHM_SRCS}
    ${BESQ_BUILTIN_STRATEGY_SRCS}
)
target_link_libraries(besq-domain-algorithm PUBLIC besq-common)

target_include_directories(besq-domain-algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/algorithm/types/Enchantment.h"
)
