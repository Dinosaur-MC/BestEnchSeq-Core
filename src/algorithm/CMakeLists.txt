# Algorithm layer sources
#
# Collects all algorithm-layer source files for use by test and benchmark targets.
# The main besq target uses its own GLOB_RECURSE src/*.cpp and does not
# need this file.
#
# Consumers: cmake -E include() from tests/CMakeLists.txt

# ── Engine core ──
set(ALGO_ENGINE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/AlgorithmExecutor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ExecutionContext.cpp"
)

# ── Forge engine ──
set(ALGO_FORGE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/forge/ForgeEngine.cpp"
)

# ── Diagnostics core ──
set(ALGO_DIAG_CORE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/DiagnosticsService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/DiagnosticsWriter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/AlgorithmDiagnostics.cpp"
)

# ── Strategy implementations (incl. per-algo diagnostics in strategy dirs) ──
include("${CMAKE_CURRENT_LIST_DIR}/strategies/CMakeLists.txt")

# ── Serialization ──
include("${CMAKE_CURRENT_LIST_DIR}/serialization/CMakeLists.txt")

# ── Compact types (tightly coupled with algorithm layer) ──
set(ALGO_COMPACT_TYPE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/../types/CompactedTypes.cpp"
)

# ── All algorithm sources combined (convenience) ──
set(ALGO_ALL_SRCS
    ${ALGO_ENGINE_SRCS}
    ${ALGO_FORGE_SRCS}
    ${ALGO_DIAG_CORE_SRCS}
    ${ALGO_SERIAL_SRCS}
    ${ALGO_STRATEGY_SRGS}
)
