# Algorithm strategy sources
#
# Collects all strategy .cpp files (including those in per-algorithm
# subdirectories) for use by test and benchmark targets.
# The main besq target uses its own GLOB_RECURSE src/*.cpp and does not
# need this file.

file(GLOB ALGO_STRATEGY_SRGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cpp"
)
set(ALGO_STRATEGY_SRGS ${ALGO_STRATEGY_SRGS} PARENT_SCOPE)

# Diagnostics sources (nested submodule used by strategies)
file(GLOB ALGO_DIAG_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../diagnostics/*.cpp"
)
set(ALGO_DIAG_SRCS ${ALGO_DIAG_SRCS} PARENT_SCOPE)
