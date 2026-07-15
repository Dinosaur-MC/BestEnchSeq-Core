# Project root directory (parent of benchmarks/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path
set(BENCH_INCLUDE ${PROJECT_ROOT}/src)

find_package(Threads REQUIRED)

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
    ${PROJECT_ROOT}/src/log/Logger.cpp
    ${PROJECT_ROOT}/src/adapters/CompactAdapter.cpp
    ${PROJECT_ROOT}/src/parsers/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parsers/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/adapters/RegistryResolver.cpp
    ${PROJECT_ROOT}/src/data/DataLoader.cpp
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData.cpp"
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
target_include_directories(forge_benchmark PRIVATE ${BENCH_INCLUDE} ${BESQ_GENERATED_DIR})

# ─── queue_benchmark (standalone, no project deps beyond src/utils) ───
add_executable(queue_benchmark
    queue_benchmark.cpp
)
target_include_directories(queue_benchmark PRIVATE ${BENCH_INCLUDE})

# ─── eloop_benchmark (standalone, depends on EventLoop + queue headers) ───
add_executable(eloop_benchmark
    eloop_benchmark.cpp
)
target_include_directories(eloop_benchmark PRIVATE ${BENCH_INCLUDE})

# Quick smoke test for ctest — runs one small case with greedy algo (< 5s)
add_test(NAME benchmark_smoke
    COMMAND forge_benchmark --test crossbow --algo greedy
    WORKING_DIRECTORY ${PROJECT_ROOT})
