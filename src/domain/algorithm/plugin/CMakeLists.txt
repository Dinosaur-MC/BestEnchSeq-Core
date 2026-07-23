# ── Algorithm plugin loader source files ─────────────────────────────────
# Included from src/domain/algorithm/CMakeLists.txt.

file(GLOB BESQ_PLUGIN_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)
