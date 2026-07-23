# ── besq-domain-algorithm ──────────────────────────────────────────────────
# Algorithm domain: compact types, forge engine, strategies, diagnostics,
# serialization, plugin loader.  Depends only on besq-common.

set(BESQ_ALGORITHM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/IAlgorithm.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Enchantment.cpp
    # ── Resolvers ──
    ${CMAKE_CURRENT_SOURCE_DIR}/resolvers/ItemResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resolvers/InventoryResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Checkpoint.cpp
)

# ── Plugin loader (AlgorithmLoader.cpp) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt)

# ── Strategies (auto-discovered + registration) ──
include(${CMAKE_CURRENT_SOURCE_DIR}/_strategies/CMakeLists.txt)

# ── Library target ─────────────────────────────────────────────────────────
add_library(besq-domain-algorithm STATIC
    ${BESQ_ALGORITHM_SRCS}
    ${BESQ_PLUGIN_SRCS}
    ${BESQ_BUILTIN_STRATEGY_SRCS}
)
target_link_libraries(besq-domain-algorithm PUBLIC besq-common)

target_include_directories(besq-domain-algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "domain/algorithm/types/Enchantment.h"
)
