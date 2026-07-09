# Project root directory (parent of benchmarks/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path
set(BENCH_INCLUDE ${PROJECT_ROOT}/src)

add_executable(forge_benchmark
    forge_benchmark.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/GreedyAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/DFSAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/AStarAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/DynamicPenaltyBalancing.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/HierarchicalMergeStrategy.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/CompactGreedyAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/CompactDFSAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/strategies/CompactAStarAlgorithm.cpp
    ${PROJECT_ROOT}/src/algorithm/CompactForgeEngine.cpp
    ${PROJECT_ROOT}/src/utils/CompactAdapter.cpp
    ${PROJECT_ROOT}/src/registries/CompactedRegistries.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmExecutor.cpp
    ${PROJECT_ROOT}/src/algorithm/ExecutionContext.cpp
    ${PROJECT_ROOT}/src/algorithm/AlgorithmRegistry.cpp
    ${PROJECT_ROOT}/src/algorithm/DefaultForgeEngine.cpp
    ${PROJECT_ROOT}/src/parser/EnchInfoParser.cpp
    ${PROJECT_ROOT}/src/parser/EquipmentParser.cpp
    ${PROJECT_ROOT}/src/parser/TagResolver.cpp
    ${PROJECT_ROOT}/src/parser/ParserUtils.cpp
    ${PROJECT_ROOT}/src/io/json.cpp
    ${PROJECT_ROOT}/src/io/CsvIO.cpp
    ${PROJECT_ROOT}/src/registries/EnchantmentRegistry.cpp
    ${PROJECT_ROOT}/src/types/EnchInfo.cpp
    ${PROJECT_ROOT}/src/types/Ench.cpp
    ${PROJECT_ROOT}/src/types/EnchSet.cpp
    ${PROJECT_ROOT}/src/types/ItemStack.cpp
    ${PROJECT_ROOT}/src/types/Equipment.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentCategoryRegistry.cpp
    ${PROJECT_ROOT}/src/registries/EquipmentRegistry.cpp
    ${PROJECT_ROOT}/src/registries/PlatformConfig.cpp
)
target_include_directories(forge_benchmark PRIVATE ${BENCH_INCLUDE})
