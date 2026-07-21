# Algorithm layer source groups
# Included from root CMakeLists.txt via include()
# All paths use CMAKE_CURRENT_LIST_DIR so they resolve relative to this file.

include("${CMAKE_CURRENT_LIST_DIR}/strategies/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/serialization/CMakeLists.txt")

# ── Engine core ──
set(BESQ_ALGO_CORE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/AlgorithmExecutor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ExecutionContext.cpp"
)

# ── Forge engine ──
set(BESQ_ALGO_FORGE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/forge/ForgeEngine.cpp"
)

# ── Diagnostics ──
set(BESQ_ALGO_DIAG_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/AlgorithmDiagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/DiagnosticsService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/DiagnosticsWriter.cpp"
)
