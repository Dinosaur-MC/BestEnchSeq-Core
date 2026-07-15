# Type sources
#
# Collects all type-layer source files for use by test and benchmark targets.
# The main besq target uses its own GLOB_RECURSE src/*.cpp and does not
# need this file.
#
# Consumers: cmake -E include() from tests/CMakeLists.txt

# Core domain types
set(DOMAIN_TYPE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/Ench.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchSet.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchInfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Equipment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ItemStack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchSolution.cpp"
)

# Compact types (used by algorithm layer)
set(COMPACT_TYPE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/CompactedTypes.cpp"
)

# All type sources combined
set(TYPE_ALL_SRCS
    ${DOMAIN_TYPE_SRCS}
    ${COMPACT_TYPE_SRCS}
)
