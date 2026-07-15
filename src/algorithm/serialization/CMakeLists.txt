# Serialization sources
#
# Collects all serialization-layer source files for use by test and
# benchmark targets.  The main besq target uses its own GLOB_RECURSE
# src/*.cpp and does not need this file.
#
# Consumers: cmake -E include() from tests/CMakeLists.txt

set(ALGO_SERIAL_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/CompactSerializer.cpp"
)
