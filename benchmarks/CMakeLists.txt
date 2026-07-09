# Project root directory (parent of benchmarks/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path
set(BENCH_INCLUDE ${PROJECT_ROOT}/src)

# GLOB all algorithm and registry sources (avoids manual listing per algorithm)
file(GLOB_RECURSE ALGO_SRC CONFIGURE_DEPENDS
    ${PROJECT_ROOT}/src/algorithm/*.cpp
)
file(GLOB_RECURSE REG_SRC CONFIGURE_DEPENDS
    ${PROJECT_ROOT}/src/registries/*.cpp
)

add_executable(forge_benchmark
    forge_benchmark.cpp
    ${ALGO_SRC}
    ${REG_SRC}
    ${PROJECT_ROOT}/src/adapters/CompactAdapter.cpp
    ${PROJECT_ROOT}/src/parser/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parser/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/parser/TagResolver.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    ${PROJECT_ROOT}/src/types/CompactedTypes.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSolution.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/Equipment.cpp
)
target_include_directories(forge_benchmark PRIVATE ${BENCH_INCLUDE})
