# ─── Benchmark targets for BestEnchSeq-Core ───

# Project root (parent of benchmarks/)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Shared include path
set(BENCH_INCLUDE
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/common
)

find_package(Threads REQUIRED)

# Helper: benchmark that links besq-core
function(besq_bench NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${BENCH_INCLUDE}
        ${BESQ_GENERATED_DIR}
    )
    target_link_libraries(${NAME} PRIVATE besq-core Threads::Threads)
endfunction()

# ── forge_benchmark (full algorithm benchmark) ──
# TODO: re-enable when registries API is stabilized in benchmark harness
# besq_bench(forge_benchmark forge_benchmark.cpp)

# ── queue_benchmark (standalone, no project deps beyond src/utils) ──
add_executable(queue_benchmark queue_benchmark.cpp)
target_include_directories(queue_benchmark PRIVATE ${BENCH_INCLUDE})

# ── eloop_benchmark (standalone, depends on EventLoop + queue headers) ──
add_executable(eloop_benchmark eloop_benchmark.cpp)
target_include_directories(eloop_benchmark PRIVATE ${BENCH_INCLUDE})

# Quick smoke test for ctest — runs one small case with greedy algo (< 5s)
add_test(NAME benchmark_smoke
    COMMAND forge_benchmark --test crossbow --algo greedy
    WORKING_DIRECTORY ${PROJECT_ROOT})
