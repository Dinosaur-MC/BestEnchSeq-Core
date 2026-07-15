# Algorithm strategy sources
#
# Collects all strategy .cpp files (including per-algorithm diagnostics
# in strategy subdirectories) for use by test and benchmark targets.
# Used by: include() from src/algorithm/CMakeLists.txt
# The main besq target uses its own GLOB_RECURSE src/*.cpp and does not
# need this file.

file(GLOB ALGO_STRATEGY_SRGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cpp"
)
