# Types layer source groups
# Included from root CMakeLists.txt via include()
# Variables defined here are available to all targets.

set(BESQ_TYPE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/Ench.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchSet.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchInfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Equipment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ItemStack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EnchSolution.cpp"
)

set(BESQ_COMPACT_TYPE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/CompactedTypes.cpp"
)
